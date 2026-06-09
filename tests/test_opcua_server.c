/*
 * End-to-end tests for OPC UA Server / OPC UA 服务器端到端测试
 * 
 * Verifies server initialization, node exposure, and data quality propagation.
 * 验证服务器初始化、节点暴露及数据质量传播。
 */
#include "opcua_server.h"
#include "config.h"
#include "tag_cache.h"
#include "s7_client.h"
#include "snap7.h"

#include <open62541/client.h>
#include <open62541/client_config_default.h>
#include <open62541/client_highlevel.h>

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <unistd.h>

#define TEST_PORT 14840

static int g_pass = 0, g_fail = 0;
static void check(int cond, const char *msg)
{
    if (cond) { g_pass++; printf("  [PASS] %s\n", msg); }
    else      { g_fail++; printf("  [FAIL] %s\n", msg); }
}

/* Background thread to run server / 后台线程运行服务器 */
static OpcuaServer  *g_srv;
static volatile bool g_running;
static void *server_thread(void *arg)
{
    (void)arg;
    opcua_server_run(g_srv, &g_running);
    return NULL;
}

/* 假 PLC 收到“读”PDU 的次数：用于证明“读一个节点只触发一趟批量读” */
static volatile int g_plc_reads;
static void on_plc_read(void *usr, PSrvEvent ev, int sz)
{ (void)usr; (void)ev; (void)sz; g_plc_reads++; }

int main(void)
{
    /* ---------- 1. Configure and mock cache / 配置及模拟缓存 ---------- */
    TagCfg tags[6];
    memset(tags, 0, sizeof(tags));
    strcpy(tags[0].name,"Speed"); tags[0].type=S7_REAL;  tags[0].bit=-1;
    strcpy(tags[1].name,"Run");   tags[1].type=S7_BOOL;  tags[1].bit=0;
    strcpy(tags[2].name,"Count"); tags[2].type=S7_DINT;  tags[2].bit=-1;
    strcpy(tags[3].name,"Temp");  tags[3].type=S7_WORD;  tags[3].bit=-1;
    strcpy(tags[4].name,"BigNum");tags[4].type=S7_LREAL; tags[4].bit=-1;
    /* 第 6 个点带自定义 NodeId(ns=2;s=[9001])，验证网关按点表 NodeId 暴露 */
    strcpy(tags[5].name,"Custom");tags[5].type=S7_REAL;  tags[5].bit=-1;
    strcpy(tags[5].node_id,"ns=2;s=[9001]");

    PlcCfg plc; memset(&plc,0,sizeof(plc));
    strcpy(plc.name,"TestPLC"); plc.tags=tags; plc.tag_count=6;

    GatewayCfg cfg; memset(&cfg,0,sizeof(cfg));
    cfg.opcua_port=TEST_PORT; cfg.plcs=&plc; cfg.plc_count=1;

    TagCache cache;
    tag_cache_init(&cache, 6);
    S7Value v;
    v.type=S7_REAL;  v.as.r=3.14f;       tag_cache_set_good(&cache,0,&v);
    v.type=S7_BOOL;  v.as.b=true;        tag_cache_set_good(&cache,1,&v);
    v.type=S7_DINT;  v.as.d=256;         tag_cache_set_good(&cache,2,&v);
    v.type=S7_WORD;  v.as.u16=40000;     tag_cache_set_good(&cache,3,&v);
    v.type=S7_LREAL; v.as.lr=3.1415926;  tag_cache_set_good(&cache,4,&v);
    v.type=S7_REAL;  v.as.r=42.5f;       tag_cache_set_good(&cache,5,&v);

    /* ---------- 2. Start server in background thread / 启动服务器后台线程 ---------- */
    g_srv = opcua_server_create(&cfg, &cache, NULL);
    check(g_srv != NULL, "opcua_server_create 成功");
    if (!g_srv) { printf("\n结果: %d 通过, %d 失败\n", g_pass, g_fail); return 1; }

    g_running = true;
    pthread_t th;
    pthread_create(&th, NULL, server_thread, NULL);

    /* ---------- 3. Connect client (with retry) / 连接客户端 (带重试机制) ---------- */
    UA_Client *client = UA_Client_new();
    UA_ClientConfig_setDefault(UA_Client_getConfig(client));

    UA_StatusCode rc = UA_STATUSCODE_BAD;
    for (int i = 0; i < 50; i++) {
        rc = UA_Client_connect(client, "opc.tcp://127.0.0.1:" "14840");
        if (rc == UA_STATUSCODE_GOOD) break;
        usleep(100 * 1000);  /* 等 100ms 再试 */
    }
    if (rc != UA_STATUSCODE_GOOD) {
        printf("  [FAIL] 客户端连不上服务器: %s\n", UA_StatusCode_name(rc));
        printf("    若是权限/沙箱禁止监听端口，属环境限制，请在普通终端跑。\n");
        g_running = false; pthread_join(th, NULL);
        UA_Client_delete(client); opcua_server_destroy(g_srv); tag_cache_destroy(&cache);
        printf("\n结果: %d 通过, 1 失败\n", g_pass);
        return 1;
    }
    check(true, "客户端已连接服务器");

    /* ---------- 4. Read and verify values / 读取并验证节点数据 ---------- */
    UA_Variant val; UA_Variant_init(&val);

    rc = UA_Client_readValueAttribute(client, UA_NODEID_STRING(1,"TestPLC.Speed"), &val);
    check(rc==UA_STATUSCODE_GOOD && UA_Variant_hasScalarType(&val,&UA_TYPES[UA_TYPES_FLOAT])
          && fabsf(*(UA_Float*)val.data - 3.14f) < 1e-4f, "读 Speed = 3.14 (REAL)");
    UA_Variant_clear(&val); UA_Variant_init(&val);

    rc = UA_Client_readValueAttribute(client, UA_NODEID_STRING(1,"TestPLC.Run"), &val);
    check(rc==UA_STATUSCODE_GOOD && UA_Variant_hasScalarType(&val,&UA_TYPES[UA_TYPES_BOOLEAN])
          && *(UA_Boolean*)val.data == true, "读 Run = true (BOOL)");
    UA_Variant_clear(&val); UA_Variant_init(&val);

    rc = UA_Client_readValueAttribute(client, UA_NODEID_STRING(1,"TestPLC.Count"), &val);
    check(rc==UA_STATUSCODE_GOOD && UA_Variant_hasScalarType(&val,&UA_TYPES[UA_TYPES_INT32])
          && *(UA_Int32*)val.data == 256, "读 Count = 256 (DINT)");
    UA_Variant_clear(&val); UA_Variant_init(&val);

    rc = UA_Client_readValueAttribute(client, UA_NODEID_STRING(1,"TestPLC.Temp"), &val);
    check(rc==UA_STATUSCODE_GOOD && UA_Variant_hasScalarType(&val,&UA_TYPES[UA_TYPES_UINT16])
          && *(UA_UInt16*)val.data == 40000, "读 Temp = 40000 (WORD->UInt16)");
    UA_Variant_clear(&val); UA_Variant_init(&val);

    rc = UA_Client_readValueAttribute(client, UA_NODEID_STRING(1,"TestPLC.BigNum"), &val);
    check(rc==UA_STATUSCODE_GOOD && UA_Variant_hasScalarType(&val,&UA_TYPES[UA_TYPES_DOUBLE])
          && fabs(*(UA_Double*)val.data - 3.1415926) < 1e-9, "读 BigNum = 3.1415926 (LREAL->Double)");
    UA_Variant_clear(&val); UA_Variant_init(&val);

    /* 自定义 NodeId：按点表里的 ns=2;s=[9001] 读，应读到 42.5 (证明网关照点表 NodeId 暴露) */
    rc = UA_Client_readValueAttribute(client, UA_NODEID_STRING(2,"[9001]"), &val);
    check(rc==UA_STATUSCODE_GOOD && UA_Variant_hasScalarType(&val,&UA_TYPES[UA_TYPES_FLOAT])
          && fabsf(*(UA_Float*)val.data - 42.5f) < 1e-4f, "按自定义 NodeId ns=2;s=[9001] 读 = 42.5");
    UA_Variant_clear(&val); UA_Variant_init(&val);

    /* Modify cache, client should read new value instantly / 修改缓存，客户端应实时读取新值 */
    v.type=S7_DINT; v.as.d=999; tag_cache_set_good(&cache,2,&v);
    rc = UA_Client_readValueAttribute(client, UA_NODEID_STRING(1,"TestPLC.Count"), &val);
    check(rc==UA_STATUSCODE_GOOD && *(UA_Int32*)val.data == 999, "改缓存后读 Count = 999 (实时)");
    UA_Variant_clear(&val); UA_Variant_init(&val);

    /* ---------- 5. Quality validation (BAD status) / 质量验证 (劣质状态) ---------- */
    tag_cache_set_bad(&cache, 2);
    rc = UA_Client_readValueAttribute(client, UA_NODEID_STRING(1,"TestPLC.Count"), &val);
    check(rc != UA_STATUSCODE_GOOD, "Count 标 BAD 后客户端读到非Good状态");
    UA_Variant_clear(&val);

    /* ---------- 6. Cleanup resources / 资源清理 ---------- */
    UA_Client_disconnect(client);
    UA_Client_delete(client);
    g_running = false;
    pthread_join(th, NULL);
    opcua_server_destroy(g_srv);
    tag_cache_destroy(&cache);

    /* ===================================================================
     * 场景 B：批量攒批刷新 + “趟数”验证（连真实假 PLC）
     * 证明 batch_read 开启时：读任一过期节点 → 一次 ReadMultiVars 把同台 PLC
     * 所有过期点全刷进缓存 → 其余点命中缓存、不再读 PLC（N 点压成 1 趟）。
     * =================================================================== */
    printf("== 批量刷新：读一个节点应只触发一趟 PLC 读 ==\n");

    /* B1. 起假 PLC，DB1 灌入 25 个点的已知数据(前5个混合类型 + 后20个INT)，
           共 25 点 → 跨 2 批 ReadMultiVars(MaxVars=20)，验证完整网关栈下的大点表批量刷新。 */
    S7Object plc_srv = Srv_Create();
    static uint8_t db1[64];
    memset(db1, 0, sizeof(db1));
    db1[0]=0x3F; db1[1]=0xC0;                                 /* REAL 1.5 @0 */
    db1[4]=(777>>8)&0xFF;    db1[5]=777&0xFF;                 /* INT 777 @4 */
    db1[6]=(123456>>24)&0xFF; db1[7]=(123456>>16)&0xFF;
    db1[8]=(123456>>8)&0xFF;  db1[9]=123456&0xFF;             /* DINT 123456 @6 */
    db1[10]=(40000>>8)&0xFF;  db1[11]=40000&0xFF;             /* WORD 40000 @10 */
    db1[12]=0x01;                                            /* BOOL bit0=1 @12 */
    for (int i = 0; i < 20; i++) {                            /* 后20个 INT @14,16,...,52 → 1000+i */
        int16_t v = (int16_t)(1000 + i);
        db1[14 + i*2]     = (uint8_t)((v >> 8) & 0xFF);
        db1[14 + i*2 + 1] = (uint8_t)(v & 0xFF);
    }
    Srv_RegisterArea(plc_srv, srvAreaDB, 1, db1, sizeof(db1));
    Srv_SetReadEventsCallback(plc_srv, on_plc_read, NULL);

    int bport = 0;
    for (uint16_t pt = 1103; pt <= 1110; pt++) {
        Srv_SetParam(plc_srv, p_u16_LocalPort, &pt);
        if (Srv_StartTo(plc_srv, "127.0.0.1") == 0) { bport = pt; break; }
        Srv_Stop(plc_srv);
    }
    if (bport == 0) {
        printf("  [跳过] 场景B假PLC在1103~1110起不来(沙箱禁监听?环境限制，非代码问题)\n");
        Srv_Destroy(&plc_srv);
        printf("\n结果: %d 通过, %d 失败\n", g_pass, g_fail);
        return g_fail == 0 ? 0 : 1;
    }
    usleep(200 * 1000);

    /* B2. 网关配置：1 PLC、25 点、连上面的假 PLC、batch_read=1、TTL 5s */
    enum { NB = 25 };
    TagCfg btags[NB]; memset(btags, 0, sizeof(btags));
    strcpy(btags[0].name,"BSpeed"); btags[0].area=AREA_DB; btags[0].db=1; btags[0].start=0;  btags[0].bit=-1; btags[0].type=S7_REAL;
    strcpy(btags[1].name,"BInt");   btags[1].area=AREA_DB; btags[1].db=1; btags[1].start=4;  btags[1].bit=-1; btags[1].type=S7_INT;
    strcpy(btags[2].name,"BCount"); btags[2].area=AREA_DB; btags[2].db=1; btags[2].start=6;  btags[2].bit=-1; btags[2].type=S7_DINT;
    strcpy(btags[3].name,"BWord");  btags[3].area=AREA_DB; btags[3].db=1; btags[3].start=10; btags[3].bit=-1; btags[3].type=S7_WORD;
    strcpy(btags[4].name,"BRun");   btags[4].area=AREA_DB; btags[4].db=1; btags[4].start=12; btags[4].bit=0;  btags[4].type=S7_BOOL;
    for (int i = 0; i < 20; i++) {   /* 后20个 INT，对应 db1 @14.. → 凑够 25 点跨第2批 */
        snprintf(btags[5+i].name, sizeof(btags[5+i].name), "BI%d", i);
        btags[5+i].area=AREA_DB; btags[5+i].db=1; btags[5+i].start=14+i*2; btags[5+i].bit=-1; btags[5+i].type=S7_INT;
    }

    PlcCfg bplc; memset(&bplc, 0, sizeof(bplc));
    strcpy(bplc.name,"BulkPLC"); strcpy(bplc.ip,"127.0.0.1");
    bplc.port=bport; bplc.rack=0; bplc.slot=1; bplc.tags=btags; bplc.tag_count=NB;

    GatewayCfg bcfg; memset(&bcfg, 0, sizeof(bcfg));
    bcfg.opcua_port=TEST_PORT+1; bcfg.plcs=&bplc; bcfg.plc_count=1; bcfg.batch_read=1;

    TagCache bcache; tag_cache_init(&bcache, NB);
    tag_cache_set_ttl(&bcache, 5000);     /* 5s TTL：批量刷新后保持新鲜，便于验证命中 */
    /* 注意：故意不预灌缓存(全 UNINIT → 不新鲜)，这样第一次读才会触发批量刷新 */

    S7Conn *bconns[1]; bconns[0] = s7_conn_create(&bplc);

    g_srv = opcua_server_create(&bcfg, &bcache, bconns);
    check(g_srv != NULL, "场景B opcua_server_create 成功");

    g_running = true;
    pthread_t th2; pthread_create(&th2, NULL, server_thread, NULL);

    UA_Client *c2 = UA_Client_new();
    UA_ClientConfig_setDefault(UA_Client_getConfig(c2));
    UA_StatusCode rc2 = UA_STATUSCODE_BAD;
    for (int i = 0; i < 100; i++) {
        rc2 = UA_Client_connect(c2, "opc.tcp://127.0.0.1:14841");
        if (rc2 == UA_STATUSCODE_GOOD) break;
        usleep(100 * 1000);
    }
    check(rc2 == UA_STATUSCODE_GOOD, "场景B 客户端已连接");

    if (rc2 == UA_STATUSCODE_GOOD) {
        UA_Variant bv;

        /* B3. 计数清零，只读 1 个节点(BSpeed) */
        g_plc_reads = 0;
        UA_Variant_init(&bv);
        rc2 = UA_Client_readValueAttribute(c2, UA_NODEID_STRING(1,"BulkPLC.BSpeed"), &bv);
        usleep(150 * 1000);  /* 给 snap7 server 事件回调一点时间 */
        check(rc2==UA_STATUSCODE_GOOD && UA_Variant_hasScalarType(&bv,&UA_TYPES[UA_TYPES_FLOAT])
              && fabsf(*(UA_Float*)bv.data - 1.5f) < 1e-4f, "读 BSpeed = 1.5 (穿透到假PLC)");
        UA_Variant_clear(&bv);

        /* B3 关键证明(不依赖事件计数)：只读了 BSpeed，直接查缓存——其余点应已被
           “一并”刷新成 Good。这是批量攒批的铁证：逐点模式下此刻 BInt 还会是 UNINIT。 */
        S7Value cv; TagQuality cq;
        bool sidekick_fresh = tag_cache_get_fresh(&bcache, 1, &cv, &cq);  /* slot1 = BInt */
        check(sidekick_fresh && cq==TAG_QUALITY_GOOD && cv.type==S7_INT && cv.as.i==777,
              "只读 BSpeed，BInt 已被一并刷新进缓存(=777, Good)");

        /* 跨批证明：第 25 个点(slot24)落在第 2 批 ReadMultiVars(MaxVars=20)。只读 BSpeed
           也应把它一并刷新 → 大点表跨批在完整网关栈下成立。 */
        S7Value lv; TagQuality lq;
        bool last_fresh = tag_cache_get_fresh(&bcache, 24, &lv, &lq);  /* slot24 = 第25个点 */
        check(last_fresh && lq==TAG_QUALITY_GOOD && lv.type==S7_INT && lv.as.i==(int16_t)(1000+19),
              "只读 BSpeed，第25个点(跨到第2批)也被刷新(=1019, Good)");

        /* 佐证：snap7 按“变量”触发读事件，一次 ReadMultiVars 读 5 个点 → 5 次读事件，
           但只是 1 个请求 PDU / 1 趟网络往返。真正的趟数(ReadMultiVars 调用次数)在真链路验证。 */
        printf("    读 1 个节点 → 假 PLC 端读事件=%d 次(snap7 按变量计；25 个点在 2 批 ReadMultiVars 内读完)\n", g_plc_reads);
        check(g_plc_reads >= 1, "读 BSpeed 确实穿透读了 PLC");

        /* B4. 再读其余 4 点：都在 TTL 内、已被批量刷新 → 命中缓存、PLC 不再被读 */
        int before = g_plc_reads;
        int vals_ok = 1;
        UA_Variant_init(&bv);
        rc2 = UA_Client_readValueAttribute(c2, UA_NODEID_STRING(1,"BulkPLC.BInt"), &bv);
        if (!(rc2==UA_STATUSCODE_GOOD && UA_Variant_hasScalarType(&bv,&UA_TYPES[UA_TYPES_INT16]) && *(UA_Int16*)bv.data==777)) vals_ok=0;
        UA_Variant_clear(&bv); UA_Variant_init(&bv);
        rc2 = UA_Client_readValueAttribute(c2, UA_NODEID_STRING(1,"BulkPLC.BCount"), &bv);
        if (!(rc2==UA_STATUSCODE_GOOD && *(UA_Int32*)bv.data==123456)) vals_ok=0;
        UA_Variant_clear(&bv); UA_Variant_init(&bv);
        rc2 = UA_Client_readValueAttribute(c2, UA_NODEID_STRING(1,"BulkPLC.BWord"), &bv);
        if (!(rc2==UA_STATUSCODE_GOOD && *(UA_UInt16*)bv.data==40000)) vals_ok=0;
        UA_Variant_clear(&bv); UA_Variant_init(&bv);
        rc2 = UA_Client_readValueAttribute(c2, UA_NODEID_STRING(1,"BulkPLC.BRun"), &bv);
        if (!(rc2==UA_STATUSCODE_GOOD && *(UA_Boolean*)bv.data==true)) vals_ok=0;
        UA_Variant_clear(&bv);
        usleep(150 * 1000);
        check(vals_ok, "其余 4 点值都正确(已被批量刷新填充)");
        check(g_plc_reads == before, "读其余 4 点未再触发 PLC 读 (命中缓存，证明 N 点 1 趟)");
    }

    /* B5. 清理场景B 的网关(假 PLC 留给场景C 复用) */
    UA_Client_disconnect(c2);
    UA_Client_delete(c2);
    g_running = false;
    pthread_join(th2, NULL);
    opcua_server_destroy(g_srv);
    tag_cache_destroy(&bcache);
    s7_conn_destroy(bconns[0]);

    /* ===================================================================
     * 场景 C：batch_read=0 时运行时应退回逐点——只刷被读的那个点。
     * 复用场景B 的假 PLC 与点表，只把开关关掉、另起一个 opcua server。
     * =================================================================== */
    printf("== 关闭 batch_read：运行时应退回逐点 ==\n");
    GatewayCfg ccfg; memset(&ccfg, 0, sizeof(ccfg));
    ccfg.opcua_port=TEST_PORT+2; ccfg.plcs=&bplc; ccfg.plc_count=1; ccfg.batch_read=0;  /* 关闭批量 */

    TagCache ccache; tag_cache_init(&ccache, (size_t)NB);
    tag_cache_set_ttl(&ccache, 5000);
    S7Conn *cconns[1]; cconns[0] = s7_conn_create(&bplc);

    g_srv = opcua_server_create(&ccfg, &ccache, cconns);
    check(g_srv != NULL, "场景C opcua_server_create 成功");
    g_running = true;
    pthread_t th3; pthread_create(&th3, NULL, server_thread, NULL);

    UA_Client *c3 = UA_Client_new();
    UA_ClientConfig_setDefault(UA_Client_getConfig(c3));
    UA_StatusCode rc3 = UA_STATUSCODE_BAD;
    for (int i = 0; i < 100; i++) {
        rc3 = UA_Client_connect(c3, "opc.tcp://127.0.0.1:14842");
        if (rc3 == UA_STATUSCODE_GOOD) break;
        usleep(100 * 1000);
    }
    check(rc3 == UA_STATUSCODE_GOOD, "场景C 客户端已连接");

    if (rc3 == UA_STATUSCODE_GOOD) {
        UA_Variant cvar; UA_Variant_init(&cvar);
        rc3 = UA_Client_readValueAttribute(c3, UA_NODEID_STRING(1,"BulkPLC.BSpeed"), &cvar);
        usleep(150 * 1000);
        check(rc3==UA_STATUSCODE_GOOD && UA_Variant_hasScalarType(&cvar,&UA_TYPES[UA_TYPES_FLOAT])
              && fabsf(*(UA_Float*)cvar.data - 1.5f) < 1e-4f, "场景C 读 BSpeed = 1.5");
        UA_Variant_clear(&cvar);

        /* 关键：逐点模式下只读了 BSpeed，BInt 不应被刷新(仍 UNINIT/不新鲜) */
        S7Value xv; TagQuality xq;
        bool int_fresh = tag_cache_get_fresh(&ccache, 1, &xv, &xq);
        check(!int_fresh, "batch_read=0：只读 BSpeed，BInt 未被刷新(运行时退回逐点，开关生效)");
    }

    UA_Client_disconnect(c3);
    UA_Client_delete(c3);
    g_running = false;
    pthread_join(th3, NULL);
    opcua_server_destroy(g_srv);
    tag_cache_destroy(&ccache);
    s7_conn_destroy(cconns[0]);

    /* 两个场景共用的假 PLC 到此销毁 */
    Srv_Stop(plc_srv);
    Srv_Destroy(&plc_srv);

    printf("\n结果: %d 通过, %d 失败\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
