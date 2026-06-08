#ifndef OPCUA_SERVER_H
#define OPCUA_SERVER_H

/*
 * OPC UA Server Integration / OPC UA 服务端集成
 * 
 * Exposes cached PLC data as OPC UA nodes using open62541.
 * 基于 open62541 将缓存的 PLC 数据暴露为 OPC UA 节点。
 */

#include "config.h"
#include "tag_cache.h"
#include "s7_client.h"
#include <stdbool.h>

typedef struct OpcuaServer OpcuaServer;

/*
 * Create OPC UA Server and Address Space / 创建 OPC UA 服务器及地址空间
 */
OpcuaServer *opcua_server_create(const GatewayCfg *cfg, TagCache *caches, S7Conn **conns);

/*
 * Run the OPC UA Server blocking loop / 运行 OPC UA 服务器阻塞循环
 */
int opcua_server_run(OpcuaServer *s, volatile bool *running);

/*
 * Destroy the OPC UA Server / 销毁 OPC UA 服务器
 */
void opcua_server_destroy(OpcuaServer *s);

#endif /* OPCUA_SERVER_H */
