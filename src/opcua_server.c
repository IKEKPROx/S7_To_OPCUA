#include "opcua_server.h"
#include <open62541/server.h>
#include <open62541/server_config_default.h>
#include <open62541/plugin/log_stdout.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* 节点上下文 / Node Context 
 * 关联 OPC UA 节点与特定 Tag 缓存槽 / Associates OPC UA node with specific cache slot */
typedef struct {
    TagCache  *cache;   /* 关联 PLC 缓存 / Associated PLC cache */
    size_t     slot;    /* 缓存槽索引 / Cache slot index */
    S7DataType type;    /* 源数据类型 / Source data type */
} NodeCtx;

struct OpcuaServer {
    UA_Server *server;
    NodeCtx   *ctxs;     /* 节点上下文数组 / Array of node contexts */
    size_t     ctx_count;
    UA_Logger  logger;   /* 自定义日志记录器 / Custom logger instance */
};

/* 类型映射：S7 -> OPC UA / Type Mapping: S7 -> OPC UA */
static const UA_DataType *ua_type_of(S7DataType t)
{
    switch (t) {
        case S7_BOOL:  return &UA_TYPES[UA_TYPES_BOOLEAN];
        case S7_INT:   return &UA_TYPES[UA_TYPES_INT16];
        case S7_DINT:  return &UA_TYPES[UA_TYPES_INT32];
        case S7_REAL:  return &UA_TYPES[UA_TYPES_FLOAT];
        case S7_BYTE:
        case S7_USINT: return &UA_TYPES[UA_TYPES_BYTE];     
        case S7_SINT:  return &UA_TYPES[UA_TYPES_SBYTE];    
        case S7_WORD:
        case S7_UINT:  return &UA_TYPES[UA_TYPES_UINT16];
        case S7_DWORD:
        case S7_UDINT: return &UA_TYPES[UA_TYPES_UINT32];
        case S7_LREAL: return &UA_TYPES[UA_TYPES_DOUBLE];
    }
    return &UA_TYPES[UA_TYPES_BOOLEAN];
}

/* OPC UA 数据源读取回调 / OPC UA DataSource Read Callback */
static UA_StatusCode
read_from_cache(UA_Server *server, const UA_NodeId *sessionId, void *sessionContext,
                const UA_NodeId *nodeId, void *nodeContext,
                UA_Boolean includeSourceTimeStamp,
                const UA_NumericRange *range, UA_DataValue *value)
{
    (void)server; (void)sessionId; (void)sessionContext; (void)nodeId; (void)range;
    NodeCtx *ctx = (NodeCtx *)nodeContext;

    /* 获取缓存快照 / Retrieve cache snapshot */
    S7Value v; TagQuality q; int64_t ts_ms;
    if (tag_cache_get(ctx->cache, ctx->slot, &v, &q, &ts_ms) != 0)
        return UA_STATUSCODE_BADINTERNALERROR;

    /* 转换为 OPC UA 标量类型 / Convert to OPC UA scalar type */
    UA_StatusCode rc;
    switch (ctx->type) {
        case S7_BOOL: { UA_Boolean x = v.as.b;
            rc = UA_Variant_setScalarCopy(&value->value, &x, &UA_TYPES[UA_TYPES_BOOLEAN]); break; }
        case S7_INT:  { UA_Int16 x = v.as.i;
            rc = UA_Variant_setScalarCopy(&value->value, &x, &UA_TYPES[UA_TYPES_INT16]); break; }
        case S7_DINT: { UA_Int32 x = v.as.d;
            rc = UA_Variant_setScalarCopy(&value->value, &x, &UA_TYPES[UA_TYPES_INT32]); break; }
        case S7_REAL: { UA_Float x = v.as.r;
            rc = UA_Variant_setScalarCopy(&value->value, &x, &UA_TYPES[UA_TYPES_FLOAT]); break; }
        case S7_BYTE:
        case S7_USINT: { UA_Byte x = v.as.u8;
            rc = UA_Variant_setScalarCopy(&value->value, &x, &UA_TYPES[UA_TYPES_BYTE]); break; }
        case S7_SINT: { UA_SByte x = v.as.i8;
            rc = UA_Variant_setScalarCopy(&value->value, &x, &UA_TYPES[UA_TYPES_SBYTE]); break; }
        case S7_WORD:
        case S7_UINT: { UA_UInt16 x = v.as.u16;
            rc = UA_Variant_setScalarCopy(&value->value, &x, &UA_TYPES[UA_TYPES_UINT16]); break; }
        case S7_DWORD:
        case S7_UDINT: { UA_UInt32 x = v.as.u32;
            rc = UA_Variant_setScalarCopy(&value->value, &x, &UA_TYPES[UA_TYPES_UINT32]); break; }
        case S7_LREAL: { UA_Double x = v.as.lr;
            rc = UA_Variant_setScalarCopy(&value->value, &x, &UA_TYPES[UA_TYPES_DOUBLE]); break; }
        default: return UA_STATUSCODE_BADINTERNALERROR;
    }
    if (rc != UA_STATUSCODE_GOOD) return rc;
    value->hasValue = true;

    /* 映射数据质量 / Map data quality to OPC UA status */
    value->status = (q == TAG_QUALITY_GOOD)
                        ? UA_STATUSCODE_GOOD
                        : UA_STATUSCODE_BADNOCOMMUNICATION;
    value->hasStatus = true;

    /* 附加时间戳 / Append source timestamp */
    if (includeSourceTimeStamp && ts_ms > 0) {
        value->sourceTimestamp = UA_DateTime_fromUnixTime(ts_ms / 1000)
                                 + (ts_ms % 1000) * UA_DATETIME_MSEC;
        value->hasSourceTimestamp = true;
    }
    return UA_STATUSCODE_GOOD;
}

/* 创建目录对象节点 / Create directory object node */
static UA_StatusCode add_object(UA_Server *server, UA_NodeId parent,
                                const char *name, UA_NodeId *out)
{
    UA_ObjectAttributes attr = UA_ObjectAttributes_default;
    attr.displayName = UA_LOCALIZEDTEXT("en-US", (char *)name);
    UA_StatusCode st = UA_Server_addObjectNode(
        server,
        UA_NODEID_STRING(1, (char *)name),
        parent,
        UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES),
        UA_QUALIFIEDNAME(1, (char *)name),
        UA_NODEID_NUMERIC(0, UA_NS0ID_BASEOBJECTTYPE),
        attr, NULL, out);
    if (st != UA_STATUSCODE_GOOD)
        fprintf(stderr, "opcua: 创建对象节点失败 / Failed to create object node \"%s\": %s\n",
                name, UA_StatusCode_name(st));
    return st;
}

/* 创建数据源变量节点 / Create DataSource variable node */
static UA_StatusCode add_tag_node(UA_Server *server, UA_NodeId parent,
                                  const char *node_id_str, const char *display,
                                  S7DataType type, NodeCtx *ctx)
{
    UA_VariableAttributes attr = UA_VariableAttributes_default;
    attr.displayName = UA_LOCALIZEDTEXT("en-US", (char *)display);
    attr.dataType    = ua_type_of(type)->typeId;
    attr.valueRank   = UA_VALUERANK_SCALAR;
    attr.accessLevel = UA_ACCESSLEVELMASK_READ;   

    UA_DataSource ds;
    ds.read  = read_from_cache;
    ds.write = NULL;                               

    UA_StatusCode st = UA_Server_addDataSourceVariableNode(
        server,
        UA_NODEID_STRING(1, (char *)node_id_str),
        parent,
        UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT),
        UA_QUALIFIEDNAME(1, (char *)display),
        UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE),
        attr, ds, ctx, NULL);
    if (st != UA_STATUSCODE_GOOD)
        fprintf(stderr, "opcua: 创建变量节点失败 / Failed to create variable node \"%s\": %s\n",
                node_id_str, UA_StatusCode_name(st));
    return st;
}

OpcuaServer *opcua_server_create(const GatewayCfg *cfg, TagCache *caches)
{
    if (!cfg || !caches) return NULL;

    OpcuaServer *s = calloc(1, sizeof(OpcuaServer));
    if (!s) return NULL;

    /* 分配节点上下文内存 / Allocate memory for node contexts */
    size_t total = 0;
    for (size_t p = 0; p < cfg->plc_count; p++) total += cfg->plcs[p].tag_count;
    s->ctxs = calloc(total ? total : 1, sizeof(NodeCtx));
    if (!s->ctxs) { free(s); return NULL; }
    s->ctx_count = total;

    /* 配置日志并初始化服务 / Configure logger and initialize server */
    s->logger = UA_Log_Stdout_withLevel(UA_LOGLEVEL_WARNING);
    UA_ServerConfig config;
    memset(&config, 0, sizeof(config));
    config.logging = &s->logger;
    UA_ServerConfig_setMinimal(&config, (UA_UInt16)cfg->opcua_port, NULL);
    s->server = UA_Server_newWithConfig(&config);
    if (!s->server) { free(s->ctxs); free(s); return NULL; }

    /* 构建 OPC UA 地址空间 / Build OPC UA Address Space */
    UA_NodeId objects = UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER);
    size_t k = 0;
    char node_id_str[160];
    for (size_t p = 0; p < cfg->plc_count; p++) {
        const PlcCfg *plc = &cfg->plcs[p];
        UA_NodeId plc_obj = UA_NODEID_NULL;
        
        if (add_object(s->server, objects, plc->name, &plc_obj) != UA_STATUSCODE_GOOD) {
            k += plc->tag_count;
            continue;
        }

        for (size_t t = 0; t < plc->tag_count; t++) {
            const TagCfg *tag = &plc->tags[t];
            s->ctxs[k].cache = &caches[p];
            s->ctxs[k].slot  = t;
            s->ctxs[k].type  = tag->type;

            snprintf(node_id_str, sizeof(node_id_str), "%s.%s", plc->name, tag->name);
            add_tag_node(s->server, plc_obj, node_id_str, tag->name,
                         tag->type, &s->ctxs[k]);
            k++;
        }
    }
    return s;
}

int opcua_server_run(OpcuaServer *s, volatile bool *running)
{
    if (!s || !s->server) return -1;
    UA_StatusCode rc = UA_Server_run(s->server, (const volatile UA_Boolean *)running);
    return rc == UA_STATUSCODE_GOOD ? 0 : -1;
}

void opcua_server_destroy(OpcuaServer *s)
{
    if (!s) return;
    if (s->server) UA_Server_delete(s->server);
    free(s->ctxs);
    free(s);
}
