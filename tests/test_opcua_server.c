/*
 * End-to-end tests for OPC UA Server / OPC UA 服务器端到端测试
 * 
 * Verifies server initialization, node exposure, and data quality propagation.
 * 验证服务器初始化、节点暴露及数据质量传播。
 */
#include "opcua_server.h"
#include "config.h"
#include "tag_cache.h"

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

    printf("\n结果: %d 通过, %d 失败\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
