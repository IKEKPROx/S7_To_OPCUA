/*
 * ua_monitor —— 终端版 OPC UA 监控面板（替代 UaExpert，适合 macOS）。
 *
 * 它读和网关同一份配置（知道有哪些 PLC、哪些点），连上网关的 OPC UA 服务，
 * 每秒刷新一次，把所有点的实时值打印成表格。掉线的点会显示坏质量。
 *
 * 用法：  ./ua_monitor [配置文件]
 *         默认 config/gateway.json。要监控假 PLC 就用 config/fake_plc.json。
 *         它连接 opc.tcp://127.0.0.1:<配置里的 opcua 端口>。
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
#include <time.h>
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

static void json_escape(const char *src, char *dst, size_t n)
{
    size_t j = 0;
    if (n == 0) return;
    for (size_t i = 0; src[i] && j + 1 < n; i++) {
        unsigned char c = (unsigned char)src[i];
        if ((c == '"' || c == '\\') && j + 2 < n) {
            dst[j++] = '\\';
            dst[j++] = (char)c;
        } else if (c >= 0x20) {
            dst[j++] = (char)c;
        }
    }
    dst[j] = '\0';
}

int main(int argc, char **argv)
{
    int json_lines = 0;
    const char *cfg_path = "config/gateway.json";
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--json-lines") == 0) {
            json_lines = 1;
        } else {
            cfg_path = argv[i];
        }
    }

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
    snprintf(url, sizeof(url), "opc.tcp://127.0.0.1:%d", cfg.opcua_port);
    if (!json_lines) printf("连接 %s ...\n", url);
    if (UA_Client_connect(client, url) != UA_STATUSCODE_GOOD) {
        fprintf(stderr, "连不上网关 %s（网关起了吗？端口对吗？）\n", url);
        UA_Client_delete(client); config_free(&cfg);
        return 1;
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    /* 主循环：每秒刷新一次 */
    while (!g_stop) {
        if (!json_lines) {
            printf("\033[2J\033[H");   /* 清屏 + 光标回左上角 */
            printf("OPC UA 监控  %s   (Ctrl+C 退出)\n", url);
        }

        for (size_t p = 0; p < cfg.plc_count && !g_stop; p++) {
            PlcCfg *plc = &cfg.plcs[p];
            if (!json_lines) {
                printf("\n● %s  (%s:%d)\n", plc->name, plc->ip, plc->port);
                printf("  %-20s %-12s %s\n", "点名", "值", "质量");
                printf("  ---------------------------------------------\n");
            }

            for (size_t t = 0; t < plc->tag_count; t++) {
                char node_id[160], valbuf[48];
                char plc_json[160], tag_json[160], node_json[320], value_json[96];
                snprintf(node_id, sizeof(node_id), "%s.%s", plc->name, plc->tags[t].name);

                UA_Variant v; UA_Variant_init(&v);
                UA_StatusCode rc = UA_Client_readValueAttribute(
                    client, UA_NODEID_STRING(1, node_id), &v);

                if (rc == UA_STATUSCODE_GOOD) {
                    format_value(&v, valbuf, sizeof(valbuf));
                    if (json_lines) {
                        json_escape(plc->name, plc_json, sizeof(plc_json));
                        json_escape(plc->tags[t].name, tag_json, sizeof(tag_json));
                        json_escape(node_id, node_json, sizeof(node_json));
                        json_escape(valbuf, value_json, sizeof(value_json));
                        printf("{\"ts\":%ld,\"plc\":\"%s\",\"tag\":\"%s\",\"node\":\"%s\",\"value\":\"%s\",\"quality\":\"Good\",\"ok\":true}\n",
                               (long)time(NULL), plc_json, tag_json, node_json, value_json);
                    } else {
                        printf("  %-20s %-12s %s\n", plc->tags[t].name, valbuf, "Good");
                    }
                } else {
                    if (json_lines) {
                        json_escape(plc->name, plc_json, sizeof(plc_json));
                        json_escape(plc->tags[t].name, tag_json, sizeof(tag_json));
                        json_escape(node_id, node_json, sizeof(node_json));
                        printf("{\"ts\":%ld,\"plc\":\"%s\",\"tag\":\"%s\",\"node\":\"%s\",\"value\":\"-\",\"quality\":\"BAD\",\"ok\":false}\n",
                               (long)time(NULL), plc_json, tag_json, node_json);
                    } else {
                        printf("  %-20s %-12s %s\n", plc->tags[t].name, "-",
                               "BAD(掉线?)");
                    }
                }
                UA_Variant_clear(&v);
            }
        }
        fflush(stdout);
        sleep(1);
    }

    printf("\n退出监控。\n");
    UA_Client_disconnect(client);
    UA_Client_delete(client);
    config_free(&cfg);
    return 0;
}
