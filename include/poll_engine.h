#ifndef POLL_ENGINE_H
#define POLL_ENGINE_H

/*
 * 轮询引擎模块 / Polling Engine Module
 * 为每台 PLC 分配独立线程进行循环采集 / Allocates independent threads per PLC for cyclic polling
 */

#include "config.h"
#include "tag_cache.h"

/* 不透明引擎结构 / Opaque Engine Structure */
typedef struct PollEngine PollEngine;

/*
 * 创建轮询引擎 / Create polling engine
 * 返回引擎实例，失败返回 NULL / Returns engine instance, NULL on failure
 */
PollEngine *poll_engine_create(const GatewayCfg *cfg, TagCache *caches);

/*
 * 启动所有轮询线程 / Start all polling threads
 * 成功返回 0 / Returns 0 on success
 */
int poll_engine_start(PollEngine *pe);

/*
 * 停止并回收所有线程 / Stop and join all threads
 */
void poll_engine_stop(PollEngine *pe);

/*
 * 销毁引擎实例 / Destroy engine instance
 */
void poll_engine_destroy(PollEngine *pe);

#endif /* POLL_ENGINE_H */
