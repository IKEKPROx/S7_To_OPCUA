#ifndef S7_CLIENT_H
#define S7_CLIENT_H

/*
 * S7 客户端模块 / S7 Client Module
 * 封装 snap7 库，处理单台 PLC 的连接与读取 / Wraps snap7 library to handle single PLC connection and reading
 */

#include "config.h"     
#include "s7_types.h"   
#include <stdbool.h>

/* 不透明连接句柄 / Opaque Connection Handle */
typedef struct S7Conn S7Conn;

/*
 * 创建连接对象 / Create connection object
 * 返回连接实例，失败返回 NULL / Returns connection instance, NULL on failure
 */
S7Conn *s7_conn_create(const PlcCfg *cfg);

/*
 * 建立 PLC 连接 / Connect to PLC
 * 成功返回 0，失败返回 snap7 错误码 / Returns 0 on success, snap7 error code on failure
 */
int s7_conn_connect(S7Conn *c);

/*
 * 检查连接状态 / Check connection status
 */
bool s7_conn_is_connected(S7Conn *c);

/*
 * 读取单个点位数据 / Read single tag value
 * 成功返回 0，失败返回 snap7 错误码 / Returns 0 on success, snap7 error code on failure
 */
int s7_conn_read_tag(S7Conn *c, const TagCfg *tag, S7Value *out);

/*
 * 获取错误描述文本 / Get error description text
 */
void s7_conn_error_text(int err, char *buf, size_t size);

/*
 * 断开当前连接 / Disconnect current connection
 */
void s7_conn_disconnect(S7Conn *c);

/*
 * 销毁连接实例并释放资源 / Destroy connection instance and release resources
 */
void s7_conn_destroy(S7Conn *c);

#endif /* S7_CLIENT_H */
