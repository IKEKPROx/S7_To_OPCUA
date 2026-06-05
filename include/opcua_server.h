#ifndef OPCUA_SERVER_H
#define OPCUA_SERVER_H

/*
 * OPC UA 服务模块 / OPC UA Server Module
 * 将缓存数据暴露为 OPC UA 变量节点 / Exposes cached data as OPC UA variable nodes
 */

#include "config.h"
#include "tag_cache.h"
#include <stdbool.h>

/* 不透明服务器结构 / Opaque Server Structure */
typedef struct OpcuaServer OpcuaServer;

/*
 * 创建并配置 OPC UA 服务器 / Create and configure OPC UA server
 * 返回服务器实例，失败返回 NULL / Returns server instance, NULL on failure
 */
OpcuaServer *opcua_server_create(const GatewayCfg *cfg, TagCache *caches);

/*
 * 运行服务器 (阻塞调用) / Run server (Blocking call)
 * 返回 0 正常退出，非 0 异常退出 / Returns 0 on normal exit, non-zero on error
 */
int opcua_server_run(OpcuaServer *s, volatile bool *running);

/*
 * 销毁服务器实例 / Destroy server instance
 */
void opcua_server_destroy(OpcuaServer *s);

#endif /* OPCUA_SERVER_H */
