#ifndef TAG_CACHE_H
#define TAG_CACHE_H

/*
 * 线程安全缓存模块 / Thread-Safe Cache Module
 * 基于互斥锁实现读写解耦的数据快照存储 / Mutex-based snapshot storage for decoupled read/write
 */

#include "s7_types.h"   
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

/* 数据质量标识 / Data Quality Status */
typedef enum {
    TAG_QUALITY_UNINIT,   /* 未初始化 / Uninitialized */
    TAG_QUALITY_GOOD,     /* 数据正常 / Good quality */
    TAG_QUALITY_BAD       /* 通信异常 / Bad quality (Communication lost) */
} TagQuality;

/* 缓存槽位结构 / Cache Slot Structure */
typedef struct {
    S7Value    value;          /* 当前值或保留历史值 / Current or retained historical value */
    TagQuality quality;        /* 数据质量 / Quality status */
    int64_t    timestamp_ms;   /* 更新时间戳(毫秒) / Update timestamp (ms) */
} TagSlot;

/* 缓存池结构 / Cache Pool Structure */
typedef struct {
    TagSlot        *slots;     /* 槽位数组 / Array of slots */
    size_t          count;     /* 槽位数量 / Slots count */
    pthread_mutex_t lock;      /* 并发互斥锁 / Mutex for concurrency */
} TagCache;

/*
 * 初始化缓存池 / Initialize cache pool
 * 成功返回 0，失败返回 -1 / Returns 0 on success, -1 on failure
 */
int tag_cache_init(TagCache *c, size_t n);

/*
 * 销毁缓存池 / Destroy cache pool
 */
void tag_cache_destroy(TagCache *c);

/* ---- 写操作 (Write Operations) ---- */

/* 更新成功数据 / Update with good data */
void tag_cache_set_good(TagCache *c, size_t i, const S7Value *v);

/* 将单一槽位标记为异常 / Mark single slot as bad quality */
void tag_cache_set_bad(TagCache *c, size_t i);

/* 将全部槽位标记为异常 / Mark all slots as bad quality */
void tag_cache_set_all_bad(TagCache *c);

/* ---- 读操作 (Read Operations) ---- */

/*
 * 获取数据快照 / Get data snapshot
 * 成功返回 0，越界返回 -1 / Returns 0 on success, -1 on out-of-bounds
 */
int tag_cache_get(TagCache *c, size_t i, S7Value *out, TagQuality *q, int64_t *ts);

#endif /* TAG_CACHE_H */
