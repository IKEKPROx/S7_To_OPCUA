#include "tag_cache.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Get current time in milliseconds / 获取当前毫秒时间戳 */
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
    c->ttl_ms = 0;   /* Default to no expiration; set via set_ttl as needed / 默认永不过期；按需采集时由 set_ttl 设置 */

    /* calloc initializes quality to TAG_QUALITY_UNINIT (0), only mutex requires initialization / calloc 已将 quality 初始化为 TAG_QUALITY_UNINIT (0)，此处仅需初始化互斥锁 */
    if (pthread_mutex_init(&c->lock, NULL) != 0) {
        free(c->slots);
        c->slots = NULL;
        c->count = 0;
        return -1;
    }
    return 0;
}

void tag_cache_set_ttl(TagCache *c, int64_t ttl_ms)
{
    if (!c) return;
    pthread_mutex_lock(&c->lock);
    c->ttl_ms = ttl_ms;
    pthread_mutex_unlock(&c->lock);
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

    if (out) *out = c->slots[i].value;
    if (q)   *q   = c->slots[i].quality;
    if (ts)  *ts  = c->slots[i].timestamp_ms;
    pthread_mutex_unlock(&c->lock);
    return 0;
}

bool tag_cache_get_fresh(TagCache *c, size_t i, S7Value *out, TagQuality *q)
{
    if (!c || i >= c->count) return false;
    bool fresh = false;
    pthread_mutex_lock(&c->lock);
    TagSlot *s = &c->slots[i];
    if (s->quality != TAG_QUALITY_UNINIT) {
        int64_t age = now_ms() - s->timestamp_ms;
        if (c->ttl_ms <= 0 || age <= c->ttl_ms) {
            fresh = true;
            if (out) *out = s->value;
            if (q)   *q   = s->quality;
        }
    }
    pthread_mutex_unlock(&c->lock);
    return fresh;
}
