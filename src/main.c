/*
 * Gateway Entry Point / 网关主程序入口
 * 
 * Implements S7 to OPC UA bridge with on-demand polling.
 * 实现带有按需轮询机制的 S7 到 OPC UA 桥接。
 */
#include "config.h"
#include "tag_cache.h"
#include "s7_client.h"
#include "opcua_server.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <signal.h>

/* Global running flag for graceful shutdown / 优雅退出的全局运行标志 */
static volatile bool g_running = true;

static void on_signal(int sig)
{
    (void)sig;
    g_running = false;
}

int main(int argc, char **argv)
{
    const char *cfg_path = (argc > 1) ? argv[1] : "config/gateway.json";
    int ret = 1;

    /* Enable line buffering for real-time log output / 启用行缓冲以确保实时日志输出 */
    setvbuf(stdout, NULL, _IOLBF, 0);

    /* Step 1: Load Configuration / 步骤 1: 加载配置 */
    GatewayCfg cfg;
    if (config_load(cfg_path, &cfg) != 0) {
        fprintf(stderr, "加载配置失败: %s\n", cfg_path);
        return 1;
    }
    printf("配置已加载: %s  (PLC 数: %zu, OPC UA 端口: %d, 缓存TTL: %dms)\n",
           cfg_path, cfg.plc_count, cfg.opcua_port, cfg.cache_ttl_ms);

    /* Step 2: Initialize Tag Caches / 步骤 2: 初始化数据点缓存 */
    TagCache *caches = calloc(cfg.plc_count, sizeof(TagCache));
    if (!caches) { config_free(&cfg); return 1; }
    for (size_t p = 0; p < cfg.plc_count; p++) {
        if (tag_cache_init(&caches[p], cfg.plcs[p].tag_count) != 0) {
            fprintf(stderr, "为 PLC \"%s\" 初始化缓存失败\n", cfg.plcs[p].name);
            for (size_t q = 0; q < p; q++) tag_cache_destroy(&caches[q]);
            free(caches);
            config_free(&cfg);
            return 1;
        }
        tag_cache_set_ttl(&caches[p], cfg.cache_ttl_ms);
    }

    /* Step 3: Initialize PLC Connections (Lazy Connection) / 步骤 3: 初始化 PLC 连接 (懒连接) */
    S7Conn **conns = calloc(cfg.plc_count, sizeof(S7Conn *));
    if (!conns) goto cleanup_caches;
    for (size_t p = 0; p < cfg.plc_count; p++)
        conns[p] = s7_conn_create(&cfg.plcs[p]);

    /* Step 4: Start OPC UA Server / 步骤 4: 启动 OPC UA 服务器 */
    OpcuaServer *srv = opcua_server_create(&cfg, caches, conns);
    if (!srv) { fprintf(stderr, "创建 OPC UA 服务器失败\n"); goto cleanup_conns; }

    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);

    printf("网关已启动(按需采集)。OPC UA: opc.tcp://localhost:%d   (Ctrl+C 退出)\n",
           cfg.opcua_port);
    printf("提示：没有客户端请求时，网关不会去读 PLC。\n");

    /* Block until shutdown signal is received / 阻塞直到接收到关闭信号 */
    opcua_server_run(srv, &g_running);

    printf("\n正在退出...\n");
    opcua_server_destroy(srv);
    ret = 0;

cleanup_conns:
    for (size_t p = 0; p < cfg.plc_count; p++)
        s7_conn_destroy(conns[p]);
    free(conns);
cleanup_caches:
    for (size_t p = 0; p < cfg.plc_count; p++)
        tag_cache_destroy(&caches[p]);
    free(caches);
    config_free(&cfg);
    printf("已退出。\n");
    return ret;
}
