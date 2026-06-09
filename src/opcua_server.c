#include "opcua_server.h"
#include "s7_client.h"
#include <open62541/server.h>
#include <open62541/server_config_default.h>
#include <open62541/plugin/log_stdout.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/*
 * Node Context for Read Callback / 用于读取回调的节点上下文
 */
typedef struct {
    S7Conn       *conn;
    const TagCfg *tag;
    TagCache     *cache;
    size_t        slot;
    const PlcCfg *plc;    /* 所属 PLC：批量刷新时遍历它的所有 tag / Owning PLC for batch refresh */
    bool          batch;  /* 是否走批量攒批刷新 / Batch-refresh mode */
} NodeCtx;

struct OpcuaServer {
    UA_Server *server;
    NodeCtx   *ctxs;
    size_t     ctx_count;
    UA_Logger  logger;
};

/*
 * Map S7 data type to OPC UA data type / 映射 S7 数据类型至 OPC UA 数据类型
 */
static const UA_DataType *ua_type_of(S7DataType t)
{
    switch (t) {
        case S7_BOOL:  return &UA_TYPES[UA_TYPES_BOOLEAN];
        case S7_INT:   return &UA_TYPES[UA_TYPES_INT16];
        case S7_DINT:  return &UA_TYPES[UA_TYPES_INT32];
        case S7_REAL:  return &UA_TYPES[UA_TYPES_FLOAT];
        case S7_BYTE:
        case S7_USINT: return &UA_TYPES[UA_TYPES_BYTE];     /* uint8 */
        case S7_SINT:  return &UA_TYPES[UA_TYPES_SBYTE];    /* int8  */
        case S7_WORD:
        case S7_UINT:  return &UA_TYPES[UA_TYPES_UINT16];
        case S7_DWORD:
        case S7_UDINT: return &UA_TYPES[UA_TYPES_UINT32];
        case S7_LREAL: return &UA_TYPES[UA_TYPES_DOUBLE];
    }
    return &UA_TYPES[UA_TYPES_BOOLEAN];
}

/*
 * Convert S7Value to UA_Variant / 转换 S7Value 至 UA_Variant
 */
static UA_StatusCode fill_variant(S7DataType type, const S7Value *v, UA_DataValue *value)
{
    UA_StatusCode rc;
    switch (type) {
        case S7_BOOL: { UA_Boolean x = v->as.b;
            rc = UA_Variant_setScalarCopy(&value->value, &x, &UA_TYPES[UA_TYPES_BOOLEAN]); break; }
        case S7_INT:  { UA_Int16 x = v->as.i;
            rc = UA_Variant_setScalarCopy(&value->value, &x, &UA_TYPES[UA_TYPES_INT16]); break; }
        case S7_DINT: { UA_Int32 x = v->as.d;
            rc = UA_Variant_setScalarCopy(&value->value, &x, &UA_TYPES[UA_TYPES_INT32]); break; }
        case S7_REAL: { UA_Float x = v->as.r;
            rc = UA_Variant_setScalarCopy(&value->value, &x, &UA_TYPES[UA_TYPES_FLOAT]); break; }
        case S7_BYTE:
        case S7_USINT: { UA_Byte x = v->as.u8;
            rc = UA_Variant_setScalarCopy(&value->value, &x, &UA_TYPES[UA_TYPES_BYTE]); break; }
        case S7_SINT: { UA_SByte x = v->as.i8;
            rc = UA_Variant_setScalarCopy(&value->value, &x, &UA_TYPES[UA_TYPES_SBYTE]); break; }
        case S7_WORD:
        case S7_UINT: { UA_UInt16 x = v->as.u16;
            rc = UA_Variant_setScalarCopy(&value->value, &x, &UA_TYPES[UA_TYPES_UINT16]); break; }
        case S7_DWORD:
        case S7_UDINT: { UA_UInt32 x = v->as.u32;
            rc = UA_Variant_setScalarCopy(&value->value, &x, &UA_TYPES[UA_TYPES_UINT32]); break; }
        case S7_LREAL: { UA_Double x = v->as.lr;
            rc = UA_Variant_setScalarCopy(&value->value, &x, &UA_TYPES[UA_TYPES_DOUBLE]); break; }
        default: return UA_STATUSCODE_BADINTERNALERROR;
    }
    if (rc == UA_STATUSCODE_GOOD) value->hasValue = true;
    return rc;
}

/*
 * Read data from PLC and update cache / 穿透读取 PLC 并更新缓存
 */
static void read_plc_into_cache(NodeCtx *ctx)
{
    if (!ctx->conn) { tag_cache_set_bad(ctx->cache, ctx->slot); return; }
    if (!s7_conn_is_connected(ctx->conn)) {
        if (s7_conn_connect(ctx->conn) != 0) {
            tag_cache_set_bad(ctx->cache, ctx->slot);
            return;
        }
    }
    S7Value nv;
    if (s7_conn_read_tag(ctx->conn, ctx->tag, &nv) == 0)
        tag_cache_set_good(ctx->cache, ctx->slot, &nv);
    else
        tag_cache_set_bad(ctx->cache, ctx->slot);
}

/*
 * Batch-refresh all stale tags of one PLC / 批量刷新一台 PLC 的所有过期点
 *
 * 按需采集的核心优化：某个点过期触发读取时，不只读它自己，而是把同一台 PLC
 * 当前所有“不新鲜”的点收集起来，用 s7_conn_read_many 一次性(按 MaxVars 分批)读回、
 * 写进缓存。于是客户端一次读 N 个点 → 第一个过期点就把全部刷新了，其余 N-1 个点
 * 直接命中缓存，把 N 趟 PLC 往返压成 ceil(N/20) 趟。
 *
 * slot 槽位顺序与 plc->tags 顺序一一对应(见 create 里 slot=t)。
 */
static void refresh_plc_batch(S7Conn *conn, const PlcCfg *plc, TagCache *cache)
{
    size_t n = plc->tag_count;

    /* 四个并行数组：待读 tag 指针 / 它的缓存槽位 / 读回的值 / 逐项结果。
       刷新不是热路径(受 TTL 节流)，malloc/free 可接受；大点表也不会爆栈。 */
    const TagCfg **tg = malloc(n * sizeof(*tg));
    size_t        *sl = malloc(n * sizeof(*sl));
    S7Value       *vv = malloc(n * sizeof(*vv));
    int           *rr = malloc(n * sizeof(*rr));
    if (!tg || !sl || !vv || !rr) {
        /* malloc 失败：退化为逐点读，保证功能不挂 / Fallback to per-tag reads */
        free(tg); free(sl); free(vv); free(rr);
        for (size_t t = 0; t < n; t++) {
            if (tag_cache_get_fresh(cache, t, NULL, NULL)) continue;
            S7Value one;
            if (conn && s7_conn_is_connected(conn) &&
                s7_conn_read_tag(conn, &plc->tags[t], &one) == 0)
                tag_cache_set_good(cache, t, &one);
            else
                tag_cache_set_bad(cache, t);
        }
        return;
    }

    /* 收集当前所有“不新鲜”的点(UNINIT 或已过期)。新鲜的直接跳过，不重复读 PLC。 */
    size_t cnt = 0;
    for (size_t t = 0; t < n; t++) {
        if (tag_cache_get_fresh(cache, t, NULL, NULL)) continue;
        tg[cnt] = &plc->tags[t];
        sl[cnt] = t;
        cnt++;
    }
    if (cnt == 0) { free(tg); free(sl); free(vv); free(rr); return; }

    /* 懒连接：没连上先连；连不上(或无连接)则这些点全标坏 / Lazy connect, mark bad on failure */
    if (!conn ||
        (!s7_conn_is_connected(conn) && s7_conn_connect(conn) != 0)) {
        for (size_t j = 0; j < cnt; j++) tag_cache_set_bad(cache, sl[j]);
        free(tg); free(sl); free(vv); free(rr);
        return;
    }

    /* 一次(分批)读回，逐项写进缓存 / Bulk read, write back per item */
    s7_conn_read_many(conn, tg, cnt, vv, rr);
    for (size_t j = 0; j < cnt; j++) {
        if (rr[j] == 0) tag_cache_set_good(cache, sl[j], &vv[j]);
        else            tag_cache_set_bad(cache, sl[j]);
    }

    free(tg); free(sl); free(vv); free(rr);
}

/*
 * DataSource Read Callback (On-demand polling) / DataSource 读取回调（按需轮询）
 */
static UA_StatusCode
read_node(UA_Server *server, const UA_NodeId *sessionId, void *sessionContext,
          const UA_NodeId *nodeId, void *nodeContext,
          UA_Boolean includeSourceTimeStamp,
          const UA_NumericRange *range, UA_DataValue *value)
{
    (void)server; (void)sessionContext; (void)nodeId; (void)range;
    NodeCtx *ctx = (NodeCtx *)nodeContext;

    /* Distinguish external client requests from internal server accesses / 区分外部客户端请求与服务器内部访问 */
    bool real_client = (sessionId != NULL && sessionId->namespaceIndex != 0);

    /* Perform on-demand fetch if cached data is stale / 若缓存过期则执行按需拉取 */
    if (real_client && !tag_cache_get_fresh(ctx->cache, ctx->slot, NULL, NULL)) {
        if (ctx->batch) refresh_plc_batch(ctx->conn, ctx->plc, ctx->cache); /* 攒批：连同台 PLC 其他过期点一起刷 */
        else            read_plc_into_cache(ctx);                            /* 逐点：只刷当前点(0.1.1 行为) */
    }

    /* Retrieve active snapshot / 获取有效快照 */
    S7Value v; TagQuality q; int64_t ts_ms = 0;
    if (tag_cache_get(ctx->cache, ctx->slot, &v, &q, &ts_ms) != 0)
        return UA_STATUSCODE_BADINTERNALERROR;

    UA_StatusCode rc = fill_variant(ctx->tag->type, &v, value);
    if (rc != UA_STATUSCODE_GOOD) return rc;

    value->status = (q == TAG_QUALITY_GOOD)
                        ? UA_STATUSCODE_GOOD
                        : UA_STATUSCODE_BADNOCOMMUNICATION;
    value->hasStatus = true;

    if (includeSourceTimeStamp && ts_ms > 0) {
        value->sourceTimestamp = UA_DateTime_fromUnixTime(ts_ms / 1000)
                                 + (ts_ms % 1000) * UA_DATETIME_MSEC;
        value->hasSourceTimestamp = true;
    }
    return UA_STATUSCODE_GOOD;
}

/*
 * Add an Object node to Address Space / 在地址空间中添加对象节点
 */
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
        fprintf(stderr, "opcua: 创建对象节点 \"%s\" 失败: %s (PLC名是否重复?)\n",
                name, UA_StatusCode_name(st));
    return st;
}

/*
 * Add a DataSource Variable node / 添加 DataSource 变量节点
 */
static UA_StatusCode add_tag_node(UA_Server *server, UA_NodeId parent,
                                  UA_NodeId node_id, const char *display,
                                  NodeCtx *ctx)
{
    UA_VariableAttributes attr = UA_VariableAttributes_default;
    attr.displayName = UA_LOCALIZEDTEXT("en-US", (char *)display);
    attr.dataType    = ua_type_of(ctx->tag->type)->typeId;
    attr.valueRank   = UA_VALUERANK_SCALAR;
    attr.accessLevel = UA_ACCESSLEVELMASK_READ;

    UA_DataSource ds;
    ds.read  = read_node;
    ds.write = NULL;

    /* node_id 由调用方决定(配置里的自定义 NodeId，或自动生成的)；addNode 内部会拷贝它 */
    UA_StatusCode st = UA_Server_addDataSourceVariableNode(
        server,
        node_id,
        parent,
        UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT),
        UA_QUALIFIEDNAME(1, (char *)display),
        UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE),
        attr, ds, ctx, NULL);
    if (st != UA_STATUSCODE_GOOD)
        fprintf(stderr, "opcua: 创建变量节点 \"%s\" 失败: %s (NodeId 是否重复?)\n",
                display, UA_StatusCode_name(st));
    return st;
}

OpcuaServer *opcua_server_create(const GatewayCfg *cfg, TagCache *caches, S7Conn **conns)
{
    if (!cfg || !caches) return NULL;

    OpcuaServer *s = calloc(1, sizeof(OpcuaServer));
    if (!s) return NULL;

    /* Pre-allocate node contexts / 预分配节点上下文 */
    size_t total = 0;
    for (size_t p = 0; p < cfg->plc_count; p++) total += cfg->plcs[p].tag_count;
    s->ctxs = calloc(total ? total : 1, sizeof(NodeCtx));
    if (!s->ctxs) { free(s); return NULL; }
    s->ctx_count = total;

    /* Configure UA server with custom logger / 配置附带自定义日志的 UA 服务器 */
    s->logger = UA_Log_Stdout_withLevel(UA_LOGLEVEL_WARNING);
    UA_ServerConfig config;
    memset(&config, 0, sizeof(config));
    config.logging = &s->logger;
    UA_ServerConfig_setMinimal(&config, (UA_UInt16)cfg->opcua_port, NULL);
    s->server = UA_Server_newWithConfig(&config);
    if (!s->server) { free(s->ctxs); free(s); return NULL; }

    /* Register namespaces needed by custom NodeIds / 为点表里的自定义 NodeId 注册命名空间。
       open62541 默认只有 ns0(OPC UA) 和 ns1(本服务器)。点表里写 ns=2;s=[...] 这种，
       必须先把 ns2..max 占出来，节点才加得进去。下游按 index 读即可，URI 用占位串。 */
    UA_UInt16 max_ns = 1;
    for (size_t p = 0; p < cfg->plc_count; p++)
        for (size_t t = 0; t < cfg->plcs[p].tag_count; t++) {
            const char *nid = cfg->plcs[p].tags[t].node_id;
            if (nid[0]) {
                UA_NodeId tmp;
                if (UA_NodeId_parse(&tmp, UA_STRING((char *)nid)) == UA_STATUSCODE_GOOD) {
                    if (tmp.namespaceIndex > max_ns) max_ns = tmp.namespaceIndex;
                    UA_NodeId_clear(&tmp);
                }
            }
        }
    for (UA_UInt16 ns = 2; ns <= max_ns; ns++) {
        char uri[32];
        snprintf(uri, sizeof(uri), "urn:s7gw:ns%u", (unsigned)ns);
        UA_Server_addNamespace(s->server, uri);   /* 依次返回 2,3,...，刚好占满到 max_ns */
    }

    /* Build address space hierarchy / 构建地址空间层级 */
    UA_NodeId objects = UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER);
    size_t k = 0;
    char node_id_str[160];
    for (size_t p = 0; p < cfg->plc_count; p++) {
        const PlcCfg *plc = &cfg->plcs[p];
        UA_NodeId plc_obj = UA_NODEID_NULL;
        if (add_object(s->server, objects, plc->name, &plc_obj) != UA_STATUSCODE_GOOD) {
            /* Skip tags if parent object creation fails / 若父对象创建失败则跳过其从属节点 */
            k += plc->tag_count;
            continue;
        }

        for (size_t t = 0; t < plc->tag_count; t++) {
            const TagCfg *tag = &plc->tags[t];
            s->ctxs[k].conn  = conns ? conns[p] : NULL;
            s->ctxs[k].tag   = tag;
            s->ctxs[k].cache = &caches[p];
            s->ctxs[k].slot  = t;
            s->ctxs[k].plc   = plc;                      /* 批量刷新要遍历同 PLC 所有 tag */
            s->ctxs[k].batch = (cfg->batch_read != 0);   /* 配置开关：是否攒批 */

            /* Decide NodeId: explicit from config, else auto "PLC.tag" / 决定 NodeId：配置有就用，否则自动 */
            UA_NodeId nid;
            bool parsed = false;
            if (tag->node_id[0]) {
                if (UA_NodeId_parse(&nid, UA_STRING((char *)tag->node_id)) == UA_STATUSCODE_GOOD)
                    parsed = true;
                else
                    fprintf(stderr, "opcua: tag \"%s\" 的 node_id \"%s\" 解析失败，改用自动 NodeId\n",
                            tag->name, tag->node_id);
            }
            if (!parsed) {
                snprintf(node_id_str, sizeof(node_id_str), "%s.%s", plc->name, tag->name);
                nid = UA_NODEID_STRING(1, node_id_str);
            }
            add_tag_node(s->server, plc_obj, nid, tag->name, &s->ctxs[k]);
            if (parsed) UA_NodeId_clear(&nid);   /* parse 出来的字符串标识符是 malloc 的，加完要释放 */
            k++;
        }
    }
    return s;
}

int opcua_server_run(OpcuaServer *s, volatile bool *running)
{
    if (!s || !s->server) return -1;
    /* Run server loop / 运行服务器事件循环 */
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
