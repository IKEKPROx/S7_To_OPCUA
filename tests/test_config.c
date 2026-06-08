/*
 * Unit tests for configuration parsing / 配置解析单元测试
 * 
 * Validates JSON configuration loading and error handling.
 * 验证 JSON 配置加载及其错误处理。
 */
#include "config.h"
#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
static void check(int cond, const char *msg)
{
    if (cond) { g_pass++; printf("  [PASS] %s\n", msg); }
    else      { g_fail++; printf("  [FAIL] %s\n", msg); }
}

int main(void)
{
    printf("== Load valid configuration ==\n");
    GatewayCfg cfg;
    int rc = config_load("config/gateway.json", &cfg);
    check(rc == 0, "config_load 返回 0");
    if (rc != 0) { printf("加载失败，后续跳过\n"); return 1; }

    check(cfg.opcua_port == 4840, "opcua_port = 4840");
    check(cfg.plc_count == 2, "解析出 2 个 PLC");

    /* First PLC / 第一个 PLC */
    PlcCfg *p0 = &cfg.plcs[0];
    check(strcmp(p0->name, "Line1_PLC") == 0, "plc[0].name = Line1_PLC");
    check(strcmp(p0->ip, "192.168.0.1") == 0, "plc[0].ip = 192.168.0.1");
    check(p0->slot == 1, "plc[0].slot = 1");
    check(p0->poll_interval_ms == 200, "plc[0].poll = 200ms");
    check(p0->tag_count == 3, "plc[0] 有 3 个 tag");

    /* tag: Motor1.Speed, DB10, start0, REAL */
    TagCfg *t0 = &p0->tags[0];
    check(strcmp(t0->name, "Motor1.Speed") == 0, "tag[0].name = Motor1.Speed");
    check(t0->area == AREA_DB && t0->db == 10, "tag[0] 在 DB10");
    check(t0->type == S7_REAL, "tag[0].type = REAL");
    check(t0->bit == -1, "tag[0].bit = -1 (非BOOL)");

    /* tag: Motor1.Run, BOOL bit0 */
    TagCfg *t1 = &p0->tags[1];
    check(t1->type == S7_BOOL && t1->bit == 0, "tag[1] = BOOL bit0");

    /* Second PLC / 第二个 PLC */
    PlcCfg *p1 = &cfg.plcs[1];
    check(p1->tags[0].area == AREA_M, "plc[1].tag[0] 在 M 区");
    check(p1->tags[0].type == S7_INT, "plc[1].tag[0].type = INT");
    check(p1->poll_interval_ms == 500, "plc[1].poll = 500ms");

    config_free(&cfg);
    check(cfg.plcs == NULL && cfg.plc_count == 0, "config_free 后结构被清空");

    printf("== Load invalid configuration (Should Fail) ==\n");
    GatewayCfg bad;
    check(config_load("tests/bad_config.json", &bad) != 0,
          "BOOL 缺 bit 的配置被拒绝 (返回非0)");
    check(config_load("tests/bad_config2.json", &bad) != 0,
          "负 start / poll=0 的配置被拒绝 (范围校验)");

    printf("\n结果: %d 通过, %d 失败\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
