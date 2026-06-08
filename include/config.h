#ifndef CONFIG_H
#define CONFIG_H

/*
 * Configuration Management / 配置管理
 * 
 * Parses JSON configurations into C structures. / 将 JSON 配置文件解析为 C 结构体。
 */

#include "s7_types.h"
#include <stddef.h>

/*
 * Tag Configuration / 点位配置
 */
typedef struct {
    char       name[64];
    S7Area     area;
    int        db;
    int        start;
    int        bit;
    S7DataType type;
    char       node_id[128];  /* 可选：显式 OPC UA NodeId(如 "ns=2;s=[1001001]")；空串=网关自动生成 */
} TagCfg;

/*
 * PLC Configuration / PLC 配置
 */
typedef struct {
    char    name[64];
    char    ip[16];
    int     port;
    int     rack;
    int     slot;
    int     poll_interval_ms;
    TagCfg *tags;
    size_t  tag_count;
} PlcCfg;

/*
 * Gateway Configuration / 网关配置
 */
typedef struct {
    int     opcua_port;
    int     cache_ttl_ms;
    PlcCfg *plcs;
    size_t  plc_count;
} GatewayCfg;

/*
 * Load configuration from file / 从文件加载配置
 */
int config_load(const char *path, GatewayCfg *out);

/*
 * Free configuration resources / 释放配置资源
 */
void config_free(GatewayCfg *cfg);

#endif /* CONFIG_H */
