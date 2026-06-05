#include "poll_engine.h"
#include "s7_client.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <stdatomic.h>

/* 单个轮询线程上下文 / Single polling thread context */
typedef struct {
    const PlcCfg *plc;
    TagCache     *cache;
    pthread_t     thread;
    atomic_int    running;   /* 运行标志 / Running flag (1=run, 0=stop) */
    int           started;   /* 线程启动状态 / Thread start status */
} PollWorker;

struct PollEngine {
    PollWorker *workers;
    size_t      count;
};

/* 响应式延时(支持提前中断) / Responsive sleep (supports early interruption) */
static void responsive_sleep(atomic_int *running, int total_ms)
{
    int slept = 0;
    while (atomic_load(running) && slept < total_ms) {
        int chunk = (total_ms - slept > 50) ? 50 : (total_ms - slept);
        struct timespec ts = { chunk / 1000, (long)(chunk % 1000) * 1000000L };
        nanosleep(&ts, NULL);
        slept += chunk;
    }
}

/* 轮询线程工作主循环 / Polling thread main loop */
static void *worker_main(void *arg)
{
    PollWorker *w = (PollWorker *)arg;
    const PlcCfg *plc = w->plc;

    S7Conn *conn = s7_conn_create(plc);
    if (!conn) {
        fprintf(stderr, "[%s] 创建连接对象失败 / Failed to create connection object\n", plc->name);
        return NULL;
    }

    char errbuf[256];
    bool down_logged = false;   /* 避免重复记录掉线日志 / Prevent duplicate disconnect logs */

    while (atomic_load(&w->running)) {
        /* 1) 维持连接 / Maintain connection */
        if (!s7_conn_is_connected(conn)) {
            int err = s7_conn_connect(conn);
            if (err != 0) {
                if (!down_logged) {   
                    s7_conn_error_text(err, errbuf, sizeof(errbuf));
                    fprintf(stderr, "[%s] 连接失败 / Connection failed: %s (重试/Retrying in %dms)\n",
                            plc->name, errbuf, plc->poll_interval_ms);
                    down_logged = true;
                }
                tag_cache_set_all_bad(w->cache);
                responsive_sleep(&w->running, plc->poll_interval_ms);
                continue;
            }
            /* 恢复连接 / Connection restored */
            fprintf(stderr, "[%s] 已连接 / Connected to %s:%d\n", plc->name, plc->ip, plc->port);
            down_logged = false;
        }

        /* 2) 遍历读取点位并更新缓存 / Poll tags and update cache */
        for (size_t t = 0; t < plc->tag_count && atomic_load(&w->running); t++) {
            S7Value v;
            int err = s7_conn_read_tag(conn, &plc->tags[t], &v);
            if (err == 0) {
                tag_cache_set_good(w->cache, t, &v);
            } else {
                /* 读取失败转入重连流程 / Read failure triggers reconnection */
                tag_cache_set_all_bad(w->cache);
                if (!down_logged) {
                    s7_conn_error_text(err, errbuf, sizeof(errbuf));
                    fprintf(stderr, "[%s] 读取失败 / Read failed: %s (连接已断/Disconnected)\n",
                            plc->name, errbuf);
                    down_logged = true;
                }
                break;
            }
        }

        /* 3) 休眠等待下个周期 / Sleep until next polling cycle */
        responsive_sleep(&w->running, plc->poll_interval_ms);
    }

    s7_conn_destroy(conn);
    return NULL;
}

PollEngine *poll_engine_create(const GatewayCfg *cfg, TagCache *caches)
{
    if (!cfg || !caches) return NULL;
    PollEngine *pe = calloc(1, sizeof(PollEngine));
    if (!pe) return NULL;

    pe->workers = calloc(cfg->plc_count, sizeof(PollWorker));
    if (!pe->workers) { free(pe); return NULL; }
    pe->count = cfg->plc_count;

    for (size_t p = 0; p < cfg->plc_count; p++) {
        pe->workers[p].plc   = &cfg->plcs[p];
        pe->workers[p].cache = &caches[p];
        atomic_init(&pe->workers[p].running, 1);
        pe->workers[p].started = 0;
    }
    return pe;
}

int poll_engine_start(PollEngine *pe)
{
    if (!pe) return -1;
    for (size_t p = 0; p < pe->count; p++) {
        PollWorker *w = &pe->workers[p];
        if (pthread_create(&w->thread, NULL, worker_main, w) != 0) {
            fprintf(stderr, "[%s] 启动轮询线程失败 / Failed to start polling thread\n", w->plc->name);
            atomic_store(&w->running, 0);
            continue;
        }
        w->started = 1;
    }
    return 0;
}

void poll_engine_stop(PollEngine *pe)
{
    if (!pe) return;
    /* 广播停止信号 / Broadcast stop signal */
    for (size_t p = 0; p < pe->count; p++)
        atomic_store(&pe->workers[p].running, 0);
        
    /* 等待线程退出 / Join threads */
    for (size_t p = 0; p < pe->count; p++) {
        if (pe->workers[p].started) {
            pthread_join(pe->workers[p].thread, NULL);
            pe->workers[p].started = 0;
        }
    }
}

void poll_engine_destroy(PollEngine *pe)
{
    if (!pe) return;
    poll_engine_stop(pe);
    free(pe->workers);
    free(pe);
}
