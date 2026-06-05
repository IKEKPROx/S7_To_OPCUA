#include "tag_cache.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* 获取当前时间(毫秒) / Get current time (ms) */
static int64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

int tag_cache_init(TagCache *c, size_t n)
{
    if (!c || n == 0) return -1;

    c->slots = calloc(n, sizeof(TagSlot));
    if (!c->slots) return -1;
    c->count = n;

    /* 初始化互斥锁 / Initialize mutex */
    if (pthread_mutex_init(&c->lock, NULL) != 0) {
        free(c->slots);
        c->slots = NULL;
        c->count = 0;
        return -1;
    }
    return 0;
}

void tag_cache_destroy(TagCache *c)
{
    if (!c) return;
    if (c->slots) {
        pthread_mutex_destroy(&c->lock);
        free(c->slots);
    }
    memset(c, 0, sizeof(*c));
}

void tag_cache_set_good(TagCache *c, size_t i, const S7Value *v)
{
    if (!c || !v || i >= c->count) return;
    pthread_mutex_lock(&c->lock);
    c->slots[i].value        = *v;                 
    c->slots[i].quality      = TAG_QUALITY_GOOD;
    c->slots[i].timestamp_ms = now_ms();
    pthread_mutex_unlock(&c->lock);
}

void tag_cache_set_bad(TagCache *c, size_t i)
{
    if (!c || i >= c->count) return;
    pthread_mutex_lock(&c->lock);
    /* 仅更新质量及时间戳，保留旧值 / Update quality and timestamp only, retaining old value */
    c->slots[i].quality      = TAG_QUALITY_BAD;
    c->slots[i].timestamp_ms = now_ms();
    pthread_mutex_unlock(&c->lock);
}

void tag_cache_set_all_bad(TagCache *c)
{
    if (!c) return;
    pthread_mutex_lock(&c->lock);
    int64_t t = now_ms();
    for (size_t i = 0; i < c->count; i++) {
        c->slots[i].quality      = TAG_QUALITY_BAD;
        c->slots[i].timestamp_ms = t;
    }
    pthread_mutex_unlock(&c->lock);
}

int tag_cache_get(TagCache *c, size_t i, S7Value *out, TagQuality *q, int64_t *ts)
{
    if (!c || i >= c->count) return -1;
    pthread_mutex_lock(&c->lock);
    /* 锁内快速拷贝数据快照 / Copy data snapshot quickly within lock */
    if (out) *out = c->slots[i].value;
    if (q)   *q   = c->slots[i].quality;
    if (ts)  *ts  = c->slots[i].timestamp_ms;
    pthread_mutex_unlock(&c->lock);
    return 0;
}
