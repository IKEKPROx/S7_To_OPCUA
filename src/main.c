/*
 * S7 -> OPC UA 网关主程序 / S7 to OPC UA Gateway Main Program
 *
 * 流程 / Flow:
 * 1. 加载配置 / Load configuration
 * 2. 初始化缓存 / Initialize cache
 * 3. 启动轮询引擎 / Start polling engine
 * 4. 运行 OPC UA 服务 / Run OPC UA server
 * 5. 优雅退出 / Graceful shutdown
 */
#include "config.h"
#include "tag_cache.h"
#include "poll_engine.h"
#include "opcua_server.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <signal.h>

/* 服务运行标志 / Server running flag */
static volatile bool g_running = true;

/* 信号处理回调 / Signal handler callback */
static void on_signal(int sig)
{
    (void)sig;
    g_running = false;
}

int main(int argc, char **argv)
{
    const char *cfg_path = (argc > 1) ? argv[1] : "config/gateway.json";
    int ret = 1;

    /* 配置 stdout 行缓冲，便于实时日志 / Set stdout to line buffered for real-time logs */
    setvbuf(stdout, NULL, _IOLBF, 0);

    /* ---- 1. 加载配置 / Load Configuration ---- */
    GatewayCfg cfg;
    if (config_load(cfg_path, &cfg) != 0) {
        fprintf(stderr, "加载配置失败 / Failed to load config: %s\n", cfg_path);
        return 1;
    }
    printf("配置已加载 / Config loaded: %s  (PLC: %zu, OPC UA Port: %d)\n",
           cfg_path, cfg.plc_count, cfg.opcua_port);

    /* ---- 2. 初始化独立缓存 / Initialize Independent Caches ---- */
    TagCache *caches = calloc(cfg.plc_count, sizeof(TagCache));
    if (!caches) { config_free(&cfg); return 1; }
    for (size_t p = 0; p < cfg.plc_count; p++) {
        if (tag_cache_init(&caches[p], cfg.plcs[p].tag_count) != 0) {
            fprintf(stderr, "初始化缓存失败 / Cache init failed for PLC \"%s\"\n", cfg.plcs[p].name);
            for (size_t q = 0; q < p; q++) tag_cache_destroy(&caches[q]);
            free(caches);
            config_free(&cfg);
            return 1;
        }
    }

    /* ---- 3. 启动轮询引擎 / Start Polling Engine ---- */
    PollEngine *pe = poll_engine_create(&cfg, caches);
    if (!pe) { fprintf(stderr, "创建轮询引擎失败 / Failed to create polling engine\n"); goto cleanup_caches; }
    poll_engine_start(pe);

    /* ---- 4. 运行 OPC UA 服务 / Run OPC UA Server ---- */
    OpcuaServer *srv = opcua_server_create(&cfg, caches);
    if (!srv) { fprintf(stderr, "创建 OPC UA 服务失败 / Failed to create OPC UA server\n"); goto cleanup_engine; }

    /* 注册终止信号 / Register termination signals */
    signal(SIGINT,  on_signal);   
    signal(SIGTERM, on_signal);   

    printf("网关已启动 / Gateway started. OPC UA: opc.tcp://localhost:%d   (Ctrl+C to exit)\n",
           cfg.opcua_port);

    /* 阻塞运行 / Run blocking */
    opcua_server_run(srv, &g_running);

    printf("\n正在退出 / Shutting down...\n");
    opcua_server_destroy(srv);
    ret = 0;

cleanup_engine:
    poll_engine_destroy(pe);
cleanup_caches:
    for (size_t p = 0; p < cfg.plc_count; p++)
        tag_cache_destroy(&caches[p]);
    free(caches);
    config_free(&cfg);
    printf("已退出 / Exited.\n");
    return ret;
}
