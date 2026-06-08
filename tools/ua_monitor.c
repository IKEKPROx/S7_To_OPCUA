/*
 * ua_monitor —— 终端版 OPC UA 监控面板（替代 UaExpert，适合 macOS）。
 *
 * 它读和网关同一份配置（知道有哪些 PLC、哪些点），连上网关的 OPC UA 服务，
 * 每秒刷新一次，把所有点的实时值打印成表格。掉线的点会显示坏质量。
 *
 * 用法：  ./ua_monitor [配置文件] [刷新间隔秒]
 *         配置文件默认 config/gateway.json；监控假 PLC 用 config/fake_plc.json。
 *         刷新间隔默认 1 秒，可填小数(如 0.5)；最小 0.1 秒，防止刷太猛。
 *         它连接 opc.tcp://localhost:<配置里的 opcua 端口>。
 */
#include "config.h"

#include <open62541/client.h>
#include <open62541/client_config_default.h>
#include <open62541/client_highlevel.h>
#include <open62541/plugin/log_stdout.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

static volatile sig_atomic_t g_stop = 0;
static void on_signal(int s){ (void)s; g_stop = 1; }

/* 把一个 variant 的标量值格式化进 buf。 */
static void format_value(const UA_Variant *v, char *buf, size_t n)
{
    if      (UA_Variant_hasScalarType(v,&UA_TYPES[UA_TYPES_BOOLEAN])) snprintf(buf,n,"%s",*(UA_Boolean*)v->data?"true":"false");
    else if (UA_Variant_hasScalarType(v,&UA_TYPES[UA_TYPES_FLOAT]))   snprintf(buf,n,"%.2f",*(UA_Float*)v->data);
    else if (UA_Variant_hasScalarType(v,&UA_TYPES[UA_TYPES_DOUBLE]))  snprintf(buf,n,"%.4f",*(UA_Double*)v->data);
    else if (UA_Variant_hasScalarType(v,&UA_TYPES[UA_TYPES_INT16]))   snprintf(buf,n,"%d",*(UA_Int16*)v->data);
    else if (UA_Variant_hasScalarType(v,&UA_TYPES[UA_TYPES_INT32]))   snprintf(buf,n,"%d",*(UA_Int32*)v->data);
    else if (UA_Variant_hasScalarType(v,&UA_TYPES[UA_TYPES_UINT16]))  snprintf(buf,n,"%u",*(UA_UInt16*)v->data);
    else if (UA_Variant_hasScalarType(v,&UA_TYPES[UA_TYPES_UINT32]))  snprintf(buf,n,"%u",*(UA_UInt32*)v->data);
    else if (UA_Variant_hasScalarType(v,&UA_TYPES[UA_TYPES_BYTE]))    snprintf(buf,n,"%u",*(UA_Byte*)v->data);
    else if (UA_Variant_hasScalarType(v,&UA_TYPES[UA_TYPES_SBYTE]))   snprintf(buf,n,"%d",*(UA_SByte*)v->data);
    else snprintf(buf,n,"(?)");
}

int main(int argc, char **argv)
{
    const char *cfg_path = (argc > 1) ? argv[1] : "config/gateway.json";

    /* 刷新间隔(秒)，可选第二个参数。默认 1 秒，下限 0.1 秒。 */
    double refresh_s = (argc > 2) ? atof(argv[2]) : 1.0;
    if (refresh_s < 0.1) refresh_s = 0.1;

    GatewayCfg cfg;
    if (config_load(cfg_path, &cfg) != 0) {
        fprintf(stderr, "加载配置失败: %s\n", cfg_path);
        return 1;
    }

    /* 客户端日志调到 WARNING，免得刷屏盖住面板。 */
    UA_Client *client = UA_Client_new();
    UA_ClientConfig *cc = UA_Client_getConfig(client);
    UA_ClientConfig_setDefault(cc);
    static UA_Logger lg;
    lg = UA_Log_Stdout_withLevel(UA_LOGLEVEL_WARNING);
    cc->logging = &lg;

    char url[64];
    snprintf(url, sizeof(url), "opc.tcp://localhost:%d", cfg.opcua_port);
    printf("连接 %s ...\n", url);
    if (UA_Client_connect(client, url) != UA_STATUSCODE_GOOD) {
        fprintf(stderr, "连不上网关 %s（网关起了吗？端口对吗？）\n", url);
        UA_Client_delete(client); config_free(&cfg);
        return 1;
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    /* 主循环：每秒刷新一次 */
    while (!g_stop) {
        printf("\033[2J\033[H");   /* 清屏 + 光标回左上角 */
        printf("OPC UA 监控  %s   刷新间隔 %.1fs   (Ctrl+C 退出)\n", url, refresh_s);

        for (size_t p = 0; p < cfg.plc_count && !g_stop; p++) {
            PlcCfg *plc = &cfg.plcs[p];
            printf("\n● %s  (%s:%d)\n", plc->name, plc->ip, plc->port);
            printf("  %-20s %-12s %s\n", "点名", "值", "质量");
            printf("  ---------------------------------------------\n");

            for (size_t t = 0; t < plc->tag_count; t++) {
                char auto_id[160], valbuf[48];
                const char *nid = plc->tags[t].node_id;

                /* NodeId：点表里指定了就用它(ns=2;s=[...])，否则用自动的 ns=1;s=PLC.点名。
                   必须和网关用同一套 NodeId，否则读不到。 */
                UA_NodeId read_id; bool parsed = false;
                if (nid[0] && UA_NodeId_parse(&read_id, UA_STRING((char *)nid)) == UA_STATUSCODE_GOOD) {
                    parsed = true;
                } else {
                    snprintf(auto_id, sizeof(auto_id), "%s.%s", plc->name, plc->tags[t].name);
                    read_id = UA_NODEID_STRING(1, auto_id);
                }

                UA_Variant v; UA_Variant_init(&v);
                UA_StatusCode rc = UA_Client_readValueAttribute(client, read_id, &v);

                if (rc == UA_STATUSCODE_GOOD) {
                    format_value(&v, valbuf, sizeof(valbuf));
                    printf("  %-20s %-12s %s\n", plc->tags[t].name, valbuf, "Good");
                } else {
                    printf("  %-20s %-12s %s\n", plc->tags[t].name, "-",
                           "BAD(掉线?)");
                }
                UA_Variant_clear(&v);
                if (parsed) UA_NodeId_clear(&read_id);   /* parse 出来的标识符是 malloc 的 */
            }
        }
        fflush(stdout);
        usleep((useconds_t)(refresh_s * 1000000));   /* 按指定间隔刷新 */
    }

    printf("\n退出监控。\n");
    UA_Client_disconnect(client);
    UA_Client_delete(client);
    config_free(&cfg);
    return 0;
}
