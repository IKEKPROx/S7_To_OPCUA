/*
 * fake_plc —— 一个"假 PLC"，用 snap7 的 server 功能模拟一台真实西门子 PLC。
 *
 * 它注册一个 DB1 数据块，灌进像真实产线那样的数据，并且【随时间变化】
 * （转速波动、计数器累加、温度漂移、液位升降…），让你不用真硬件也能端到端测网关。
 *
 * 用法：  ./fake_plc [端口]
 *         默认端口 1102（避免标准 102 需要 root）。
 *         配套的网关配置见 config/fake_plc.json。
 *
 * DB1 数据布局（字节偏移 -> 类型 -> 含义）：
 *    0   REAL   Motor1_Speed     电机转速 rpm   (~1500 上下波动)
 *    4   REAL   Motor1_Current   电机电流 A     (~12 上下波动)
 *    8   BYTE   StatusBits       bit0=运行 bit1=故障 bit2=加热器
 *   10   INT    Temperature      温度 ℃         (~70 漂移)
 *   12   DINT   PartCounter      产量计数        (持续累加)
 *   16   WORD   StatusWord       状态字(无符号)  (0~65535 循环)
 *   18   DWORD  TotalRuntime     累计运行秒数    (持续累加)
 *   22   LREAL  Pressure         压力 bar(双精度)(~5.0 上下波动)
 *   30   REAL   TankLevel        储罐液位 %      (0~100 缓慢升降)
 */
#include "snap7.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <signal.h>
#include <math.h>
#include <time.h>

#define DB_NUMBER 1
#define DB_SIZE   64

/* ---- 大端写入helpers：把值按 S7 大端序摆进字节缓冲 ---- */
static void put_u16(uint8_t *b, uint16_t v){ b[0]=(uint8_t)(v>>8); b[1]=(uint8_t)v; }
static void put_u32(uint8_t *b, uint32_t v){ b[0]=(uint8_t)(v>>24); b[1]=(uint8_t)(v>>16);
                                             b[2]=(uint8_t)(v>>8);  b[3]=(uint8_t)v; }
static void put_u64(uint8_t *b, uint64_t v){ put_u32(b,(uint32_t)(v>>32)); put_u32(b+4,(uint32_t)v); }
static void put_i16(uint8_t *b, int16_t v){ put_u16(b,(uint16_t)v); }
static void put_i32(uint8_t *b, int32_t v){ put_u32(b,(uint32_t)v); }
static void put_real(uint8_t *b, float f){ uint32_t u; memcpy(&u,&f,4); put_u32(b,u); }
static void put_lreal(uint8_t *b, double d){ uint64_t u; memcpy(&u,&d,8); put_u64(b,u); }
static void set_bit(uint8_t *b, int bit, bool on){ if(on) *b|=(uint8_t)(1<<bit); else *b&=(uint8_t)~(1<<bit); }

static volatile sig_atomic_t g_stop = 0;
static void on_signal(int s){ (void)s; g_stop = 1; }

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    uint16_t port = (argc > 1) ? (uint16_t)atoi(argv[1]) : 1102;

    /* ---- 建 server，注册 DB1 ---- */
    S7Object srv = Srv_Create();
    if (srv == 0) { fprintf(stderr, "Srv_Create 失败\n"); return 1; }
    Srv_SetParam(srv, p_u16_LocalPort, &port);

    static uint8_t db1[DB_SIZE];
    memset(db1, 0, sizeof(db1));
    if (Srv_RegisterArea(srv, srvAreaDB, DB_NUMBER, db1, sizeof(db1)) != 0) {
        fprintf(stderr, "注册 DB%d 失败\n", DB_NUMBER); Srv_Destroy(&srv); return 1;
    }

    int err = Srv_StartTo(srv, "0.0.0.0");
    if (err != 0) {
        char e[256]; Srv_ErrorText(err, e, sizeof(e));
        fprintf(stderr, "启动失败(端口 %u): %s\n", port, e);
        fprintf(stderr, "若是权限问题，换个 >1024 的端口；若端口被占，换一个。\n");
        Srv_Destroy(&srv); return 1;
    }

    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);
    printf("假 PLC 已启动：监听端口 %u，DB%d 大小 %d 字节\n", port, DB_NUMBER, DB_SIZE);
    printf("本机网关请用 ip=127.0.0.1；Docker 网关请用 Mac 局域网 IP 连接，port=%u rack=0 slot=1\n", port);
    printf("Ctrl+C 退出。\n\n");

    /* ---- 模拟循环：每 500ms 更新一次数据，像真设备一样动起来 ---- */
    int32_t  part_counter = 1000;   /* 产量从 1000 起 */
    uint32_t runtime_sec  = 0;
    double   level        = 100.0;  /* 液位从满开始 */
    bool     level_filling = false;
    int      tick = 0;

    while (!g_stop) {
        double t = tick * 0.5;       /* 模拟时间(秒) */

        float speed   = 1500.0f + 50.0f  * (float)sin(t * 0.6);          /* 转速波动 */
        float current = 12.0f   + 2.0f   * (float)sin(t * 0.9 + 1.0);    /* 电流波动 */
        int16_t temp  = (int16_t)(70 + (int)(8 * sin(t * 0.3)));         /* 温度漂移 */
        double pressure = 5.0 + 0.15 * sin(t * 1.1);                     /* 压力波动 */
        bool running  = true;
        bool fault    = (tick % 40) >= 38;   /* 偶尔报一下故障(每20秒亮2秒) */
        bool heater   = temp < 68;            /* 温度低时加热器开 */
        uint16_t statusword = (uint16_t)(tick * 137);  /* 状态字滚动 */

        part_counter += (running && !fault) ? 1 : 0;   /* 正常运行才计数 */
        runtime_sec  += 1;                              /* 这里粗略当每cycle+1秒 */

        /* 液位 100->0 再 0->100 来回 */
        if (level_filling) { level += 1.5; if (level >= 100.0) { level = 100.0; level_filling = false; } }
        else               { level -= 1.0; if (level <= 0.0)   { level = 0.0;   level_filling = true;  } }

        /* 写进 DB（加锁，避免和客户端读冲突） */
        Srv_LockArea(srv, srvAreaDB, DB_NUMBER);
        put_real (db1 + 0,  speed);
        put_real (db1 + 4,  current);
        set_bit  (db1 + 8,  0, running);
        set_bit  (db1 + 8,  1, fault);
        set_bit  (db1 + 8,  2, heater);
        put_i16  (db1 + 10, temp);
        put_i32  (db1 + 12, part_counter);
        put_u16  (db1 + 16, statusword);
        put_u32  (db1 + 18, runtime_sec);
        put_lreal(db1 + 22, pressure);
        put_real (db1 + 30, (float)level);
        Srv_UnlockArea(srv, srvAreaDB, DB_NUMBER);

        /* 每 5 秒在终端打一行当前值，方便你对照 */
        if (tick % 10 == 0) {
            printf("速度=%.1frpm 电流=%.1fA 温度=%d℃ 计数=%d 压力=%.2fbar 液位=%.0f%% %s%s\n",
                   speed, current, temp, part_counter, pressure, level,
                   fault ? "[故障]" : "", heater ? "[加热]" : "");
        }

        struct timespec ts = { 0, 500 * 1000000L };  /* 500ms */
        nanosleep(&ts, NULL);
        tick++;
    }

    printf("\n假 PLC 正在关闭...\n");
    Srv_Stop(srv);
    Srv_Destroy(&srv);
    printf("已退出。\n");
    return 0;
}
