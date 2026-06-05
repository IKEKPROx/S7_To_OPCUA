#ifndef CONFIG_H
#define CONFIG_H

/*
 * 配置解析模块 / Configuration Parsing Module
 * 从 JSON 读取 PLC 与点位配置 / Loads PLC and tag configurations from JSON
 * 仅输出纯数据结构，与通信解耦 / Outputs pure data structures, decoupled from communication
 */

#include "s7_types.h"
#include <stddef.h>

/* 点位配置 / Tag Configuration */
typedef struct {
    char       name[64];  /* 节点显示名称 / Node display name (e.g., "Motor1.Speed") */
    S7Area     area;      /* 存储区 / Memory area (DB/M/I/Q) */
    int        db;        /* DB块号 / DB number (Valid if area=DB) */
    int        start;     /* 字节偏移量 / Byte offset */
    int        bit;       /* 位偏移量 / Bit offset (0~7, valid for BOOL) */
    S7DataType type;      /* 数据类型 / Data type */
} TagCfg;

/* PLC 终端配置 / PLC Terminal Configuration */
typedef struct {
    char    name[64];          /* PLC名称（作节点前缀）/ PLC name (Node prefix) */
    char    ip[16];            /* IP地址 / IP address */
    int     port;              /* S7端口 / S7 port (Default 102) */
    int     rack;              /* 机架号 / Rack number (Default 0) */
    int     slot;              /* 槽位号 / Slot number (Default 1) */
    int     poll_interval_ms;  /* 轮询间隔(毫秒) / Polling interval (ms) */
    TagCfg *tags;              /* 点位数组 / Tags array */
    size_t  tag_count;         /* 点位数量 / Tags count */
} PlcCfg;

/* 网关全局配置 / Gateway Global Configuration */
typedef struct {
    int     opcua_port;        /* OPC UA 服务端口 / OPC UA server port (Default 4840) */
    PlcCfg *plcs;              /* PLC配置数组 / PLCs array */
    size_t  plc_count;         /* PLC数量 / PLCs count */
} GatewayCfg;

/*
 * 解析配置文件 / Parse configuration file
 * 成功返回0，失败返回-1并输出错误 / Returns 0 on success, -1 on failure
 */
int config_load(const char *path, GatewayCfg *out);

/*
 * 释放配置内存 / Free configuration memory
 */
void config_free(GatewayCfg *cfg);

#endif /* CONFIG_H */
