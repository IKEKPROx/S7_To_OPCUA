/*
 * Unit tests for s7_client / s7_client 单元测试
 * 
 * Verifies S7 client connection and read operations using a mock Snap7 server.
 * 使用 Snap7 模拟服务端验证 S7 客户端的连接与读取操作。
 */
#include "s7_client.h"
#include "config.h"
#include "snap7.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int g_pass = 0, g_fail = 0;
static void check(int cond, const char *msg)
{
    if (cond) { g_pass++; printf("  [PASS] %s\n", msg); }
    else      { g_fail++; printf("  [FAIL] %s\n", msg); }
}

int main(void)
{
    /* ---------- 1. Start mock PLC (Snap7 Server) / 启动模拟 PLC ---------- */
    S7Object srv = Srv_Create();
    if (srv == 0) {
        printf("  [FAIL] Srv_Create 失败，无法创建假 PLC\n");
        printf("\n结果: %d 通过, 1 失败\n", g_pass);
        return 1;
    }

    /* Setup DB10 contents with big-endian data / 初始化 DB10 内容 */
    static uint8_t db10[16];
    memset(db10, 0, sizeof(db10));
    db10[0]=0x3F; db10[1]=0xC0; db10[2]=0x00; db10[3]=0x00;  /* 1.5f */
    db10[4]=0x01;                                            /* bit0 on */
    db10[6]=0x00; db10[7]=0x00; db10[8]=0x01; db10[9]=0x00;  /* 256 */
    int rerr = Srv_RegisterArea(srv, srvAreaDB, 10, db10, sizeof(db10));
    if (rerr != 0) {
        char emsg[256]; Srv_ErrorText(rerr, emsg, sizeof(emsg));
        printf("  [FAIL] Srv_RegisterArea 失败: %s\n", emsg);
        Srv_Destroy(&srv);
        printf("\n结果: %d 通过, 1 失败\n", g_pass);
        return 1;
    }

    /* Find an available port (1102-1110) / 寻找可用端口 */
    int chosen_port = 0;
    for (uint16_t p = 1102; p <= 1110; p++) {
        Srv_SetParam(srv, p_u16_LocalPort, &p);
        int serr = Srv_StartTo(srv, "127.0.0.1");
        if (serr == 0) { chosen_port = p; break; }
        char emsg[256];
        Srv_ErrorText(serr, emsg, sizeof(emsg));
        printf("    端口 %u 起不来: %s\n", p, emsg);
        Srv_Stop(srv);  /* 半开状态清一下再试下一个 */
    }
    if (chosen_port == 0) {
        /* Fails if no port is available / 若无可用端口则测试失败 */
        printf("  [FAIL] 假 PLC 在 1102~1110 全部启动失败\n");
        printf("    诊断提示：\n");
        printf("      - 若上面是 \"Address already in use\"：端口被占，杀掉残留进程或换端口段。\n");
        printf("      - 若是 \"Other Socket error (1)\"(EPERM/不允许)：当前环境(如沙箱)\n");
        printf("        禁止程序监听 TCP 端口，本集成测试无法在此环境运行——这是环境限制，\n");
        printf("        不是代码 bug。请在能监听本地端口的普通环境(如本机终端)里跑。\n");
        Srv_Destroy(&srv);
        printf("\n结果: %d 通过, 1 失败\n", g_pass);
        return 1;
    }
    printf("  假 PLC 已在端口 %d 启动\n", chosen_port);
    usleep(200 * 1000);  /* 给监听线程一点时间就绪 */

    /* ---------- 2. Create configuration targeting mock PLC / 创建指向模拟 PLC 的配置 ---------- */
    TagCfg tags[3];
    memset(tags, 0, sizeof(tags));
    strcpy(tags[0].name, "Speed"); tags[0].area=AREA_DB; tags[0].db=10;
        tags[0].start=0; tags[0].bit=-1; tags[0].type=S7_REAL;
    strcpy(tags[1].name, "Run");   tags[1].area=AREA_DB; tags[1].db=10;
        tags[1].start=4; tags[1].bit=0;  tags[1].type=S7_BOOL;
    strcpy(tags[2].name, "Count"); tags[2].area=AREA_DB; tags[2].db=10;
        tags[2].start=6; tags[2].bit=-1; tags[2].type=S7_DINT;

    PlcCfg plc;
    memset(&plc, 0, sizeof(plc));
    strcpy(plc.name, "Fake"); strcpy(plc.ip, "127.0.0.1");
    plc.port=chosen_port; plc.rack=0; plc.slot=1; plc.poll_interval_ms=200;
    plc.tags=tags; plc.tag_count=3;

    /* ---------- 3. Connect and read using s7_client / 使用 s7_client 连接并读取 ---------- */
    printf("== 连接假 PLC ==\n");
    S7Conn *c = s7_conn_create(&plc);
    check(c != NULL, "s7_conn_create 成功");

    int err = s7_conn_connect(c);
    if (err != 0) { char eb[256]; s7_conn_error_text(err, eb, sizeof(eb));
        printf("    连接错误: %s\n", eb); }
    check(err == 0, "s7_conn_connect 返回 0");
    check(s7_conn_is_connected(c), "状态显示已连接");

    printf("== 读取并验证 ==\n");
    S7Value v;
    check(s7_conn_read_tag(c, &tags[0], &v)==0 && v.type==S7_REAL
          && v.as.r > 1.49f && v.as.r < 1.51f, "REAL Speed = 1.5");
    check(s7_conn_read_tag(c, &tags[1], &v)==0 && v.type==S7_BOOL
          && v.as.b==true,  "BOOL Run = true");
    check(s7_conn_read_tag(c, &tags[2], &v)==0 && v.type==S7_DINT
          && v.as.d==256,   "DINT Count = 256");

    /* ---------- 4. Test disconnection / 验证掉线处理 ---------- */
    printf("== 掉线后读取应失败 ==\n");
    Srv_Stop(srv);
    usleep(200 * 1000);
    int err2 = s7_conn_read_tag(c, &tags[0], &v);
    check(err2 != 0, "PLC 停了之后 read_tag 返回非0");
    check(!s7_conn_is_connected(c), "状态显示已掉线");

    /* ---------- 5. Cleanup resources / 资源清理 ---------- */
    s7_conn_destroy(c);
    Srv_Destroy(&srv);

    printf("\n结果: %d 通过, %d 失败\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
