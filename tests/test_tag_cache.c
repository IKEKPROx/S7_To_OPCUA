/*
 * test_tag_cache —— 验证缓存的功能 + 多线程安全。
 *
 * 多线程怎么测"对不对"：
 *   - 8 个写线程，每个独占一个槽，把 DINT 值从 0 一路递增写进去。
 *   - 4 个读线程不停读所有槽。因为每个槽只有一个写者且值只增不减，
 *     读到的值就必须【单调不减】。如果锁写错了、发生撕裂读(torn read)，
 *     就可能读到乱序/垃圾值，单调性被打破 -> 测试失败。
 *   - 另外用 ThreadSanitizer 再跑一遍（make tsan），直接抓数据竞争。
 */
#include "tag_cache.h"
#include <stdio.h>
#include <pthread.h>
#include <stdatomic.h>

#define NSLOTS   8
#define NREADERS 4
#define ITERS    200000

static int g_pass = 0, g_fail = 0;
static void check(int cond, const char *msg)
{
    if (cond) { g_pass++; printf("  [PASS] %s\n", msg); }
    else      { g_fail++; printf("  [FAIL] %s\n", msg); }
}

/* ---------- 多线程部分的共享状态 ---------- */
static TagCache       g_cache;
static atomic_int     g_running;        /* 1=读线程继续跑 */
static atomic_long    g_reads;          /* 总读次数 */
static atomic_int     g_violations;     /* 单调性被破坏的次数（应为0） */

static void *writer_main(void *arg)
{
    size_t slot = (size_t)(intptr_t)arg;
    S7Value v; v.type = S7_DINT;
    for (int k = 0; k < ITERS; k++) {
        v.as.d = k;                       /* 严格递增 */
        tag_cache_set_good(&g_cache, slot, &v);
    }
    return NULL;
}

static void *reader_main(void *arg)
{
    (void)arg;
    int32_t last[NSLOTS];
    for (int i = 0; i < NSLOTS; i++) last[i] = -1;

    while (atomic_load(&g_running)) {
        for (int i = 0; i < NSLOTS; i++) {
            S7Value v; TagQuality q;
            if (tag_cache_get(&g_cache, (size_t)i, &v, &q, NULL) != 0) continue;
            if (q == TAG_QUALITY_GOOD) {
                /* 单个槽只有一个写者且只增，读到的值不该变小 */
                if (v.as.d < last[i]) atomic_fetch_add(&g_violations, 1);
                else last[i] = v.as.d;
            }
            atomic_fetch_add(&g_reads, 1);
        }
    }
    return NULL;
}

int main(void)
{
    /* ===== 1. 单线程功能测试 ===== */
    printf("== 基本功能 ==\n");
    TagCache c;
    check(tag_cache_init(&c, 3) == 0, "init 3 个槽成功");

    S7Value out; TagQuality q; int64_t ts;
    check(tag_cache_get(&c, 0, &out, &q, &ts) == 0 && q == TAG_QUALITY_UNINIT,
          "初始质量是 UNINIT");
    check(tag_cache_get(&c, 99, NULL, NULL, NULL) == -1, "越界下标返回 -1");

    S7Value v; v.type = S7_DINT; v.as.d = 42;
    tag_cache_set_good(&c, 0, &v);
    check(tag_cache_get(&c, 0, &out, &q, &ts) == 0
          && q == TAG_QUALITY_GOOD && out.as.d == 42 && ts > 0,
          "set_good 后读到 GOOD + 值42 + 有时间戳");

    tag_cache_set_bad(&c, 0);
    check(tag_cache_get(&c, 0, &out, &q, NULL) == 0
          && q == TAG_QUALITY_BAD && out.as.d == 42,
          "set_bad 后质量BAD但旧值42仍保留");

    /* 给另两个槽写值，再 set_all_bad */
    tag_cache_set_good(&c, 1, &v);
    tag_cache_set_good(&c, 2, &v);
    tag_cache_set_all_bad(&c);
    int all_bad = 1;
    for (int i = 0; i < 3; i++) {
        tag_cache_get(&c, (size_t)i, NULL, &q, NULL);
        if (q != TAG_QUALITY_BAD) all_bad = 0;
    }
    check(all_bad, "set_all_bad 把所有槽都标成 BAD");
    tag_cache_destroy(&c);
    check(c.slots == NULL && c.count == 0, "destroy 后结构清空");

    /* ===== 2. 多线程压力测试 ===== */
    printf("== 多线程并发 (8写 x %d读, 每写 %d 次) ==\n", NREADERS, ITERS);
    check(tag_cache_init(&g_cache, NSLOTS) == 0, "init 共享缓存");
    atomic_store(&g_running, 1);
    atomic_store(&g_reads, 0);
    atomic_store(&g_violations, 0);

    pthread_t writers[NSLOTS], readers[NREADERS];
    for (int i = 0; i < NREADERS; i++)
        pthread_create(&readers[i], NULL, reader_main, NULL);
    for (int i = 0; i < NSLOTS; i++)
        pthread_create(&writers[i], NULL, writer_main, (void *)(intptr_t)i);

    for (int i = 0; i < NSLOTS; i++) pthread_join(writers[i], NULL);
    atomic_store(&g_running, 0);                 /* 让读线程收工 */
    for (int i = 0; i < NREADERS; i++) pthread_join(readers[i], NULL);

    printf("  (总读取次数: %ld)\n", atomic_load(&g_reads));
    check(atomic_load(&g_reads) > 0, "读线程确实跑了");
    check(atomic_load(&g_violations) == 0, "没有撕裂读/乱序 (单调性保持)");

    /* 写完后每个槽应是最后写入的值 ITERS-1，质量 GOOD */
    int final_ok = 1;
    for (int i = 0; i < NSLOTS; i++) {
        tag_cache_get(&g_cache, (size_t)i, &out, &q, NULL);
        if (q != TAG_QUALITY_GOOD || out.as.d != ITERS - 1) final_ok = 0;
    }
    check(final_ok, "结束后每个槽都是最后写入值且 GOOD");
    tag_cache_destroy(&g_cache);

    printf("\n结果: %d 通过, %d 失败\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
