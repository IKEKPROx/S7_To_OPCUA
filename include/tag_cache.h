#ifndef TAG_CACHE_H
#define TAG_CACHE_H

/*
 * Thread-safe Tag Cache / 线程安全的点位缓存
 * 
 * Provides thread-safe caching of PLC values to prevent data races.
 * 提供线程安全的 PLC 数据缓存，防止数据竞争。
 */

#include "s7_types.h"   /* S7Value */
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Tag Quality Indicators / 点位质量标识
 */
typedef enum {
    TAG_QUALITY_UNINIT,
    TAG_QUALITY_GOOD,
    TAG_QUALITY_BAD
} TagQuality;

/*
 * Single Tag Cache Slot / 单点位缓存槽
 */
typedef struct {
    S7Value    value;
    TagQuality quality;
    int64_t    timestamp_ms;
} TagSlot;

/*
 * PLC Cache Instance / PLC 缓存实例
 */
typedef struct {
    TagSlot        *slots;
    size_t          count;
    int64_t         ttl_ms;
    pthread_mutex_t lock;
} TagCache;

/*
 * Initialize Cache / 初始化缓存
 */
int tag_cache_init(TagCache *c, size_t n);

/*
 * Set TTL for cache entries / 设置缓存过期时间
 */
void tag_cache_set_ttl(TagCache *c, int64_t ttl_ms);

/*
 * Destroy Cache / 销毁缓存
 */
void tag_cache_destroy(TagCache *c);

/* Write Operations / 写操作 */

/*
 * Set value with GOOD quality / 写入正常质量的值
 */
void tag_cache_set_good(TagCache *c, size_t i, const S7Value *v);

/*
 * Set BAD quality for a slot / 将槽标记为劣质状态
 */
void tag_cache_set_bad(TagCache *c, size_t i);

/*
 * Set BAD quality for all slots / 将所有槽标记为劣质状态
 */
void tag_cache_set_all_bad(TagCache *c);

/* Read Operations / 读操作 */

/*
 * Take a snapshot of a slot / 获取槽的数据快照
 */
int tag_cache_get(TagCache *c, size_t i, S7Value *out, TagQuality *q, int64_t *ts);

/*
 * Retrieve fresh data based on TTL / 根据过期时间检索有效数据
 * 
 * Used for on-demand polling. / 用于按需轮询机制。
 */
bool tag_cache_get_fresh(TagCache *c, size_t i, S7Value *out, TagQuality *q);

#endif /* TAG_CACHE_H */
