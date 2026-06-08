#ifndef S7_CLIENT_H
#define S7_CLIENT_H

/*
 * Snap7 Client Wrapper / Snap7 客户端封装
 * 
 * Manages PLC connections and tag reading operations.
 * 管理 PLC 连接及点位读取操作。
 */

#include "config.h"     /* PlcCfg / TagCfg */
#include "s7_types.h"   /* S7Value */
#include <stdbool.h>

typedef struct S7Conn S7Conn;

/*
 * Create an S7 connection handle / 创建 S7 连接句柄
 */
S7Conn *s7_conn_create(const PlcCfg *cfg);

/*
 * Connect to PLC / 连接至 PLC
 */
int s7_conn_connect(S7Conn *c);

/*
 * Check connection status / 检查连接状态
 */
bool s7_conn_is_connected(S7Conn *c);

/*
 * Read a single tag / 读取单个点位
 */
int s7_conn_read_tag(S7Conn *c, const TagCfg *tag, S7Value *out);

/*
 * Convert snap7 error code to text / 转换 Snap7 错误码为文本
 */
void s7_conn_error_text(int err, char *buf, size_t size);

/*
 * Disconnect from PLC / 断开 PLC 连接
 */
void s7_conn_disconnect(S7Conn *c);

/*
 * Destroy the connection handle / 销毁连接句柄
 */
void s7_conn_destroy(S7Conn *c);

#endif /* S7_CLIENT_H */
