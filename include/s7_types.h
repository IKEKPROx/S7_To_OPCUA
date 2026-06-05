#ifndef S7_TYPES_H
#define S7_TYPES_H

/*
 * 数据解析模块 / Data Parsing Module
 * 将 S7 大端序字节流解析为标准 C 数据类型 / Parses S7 big-endian byte streams into standard C types
 * 纯 C 实现，无外部库依赖 / Pure C implementation, no external dependencies
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* 支持的数据类型 / Supported Data Types */
typedef enum {
    S7_BOOL,    /* 1 bit   -> bool                  */
    S7_INT,     /* 2 bytes -> int16_t   (signed)    */
    S7_DINT,    /* 4 bytes -> int32_t   (signed)    */
    S7_REAL,    /* 4 bytes -> float     (IEEE754)   */
    S7_BYTE,    /* 1 byte  -> uint8_t   (unsigned)  */
    S7_SINT,    /* 1 byte  -> int8_t    (signed)    */
    S7_USINT,   /* 1 byte  -> uint8_t   (unsigned)  */
    S7_WORD,    /* 2 bytes -> uint16_t  (unsigned)  */
    S7_UINT,    /* 2 bytes -> uint16_t  (unsigned)  */
    S7_DWORD,   /* 4 bytes -> uint32_t  (unsigned)  */
    S7_UDINT,   /* 4 bytes -> uint32_t  (unsigned)  */
    S7_LREAL    /* 8 bytes -> double    (IEEE754)   */
} S7DataType;

/* 存储区定义 / Memory Area Definitions */
typedef enum {
    AREA_DB,    /* 数据块 / Data Block (DB) */
    AREA_M,     /* 位存储区 / Merker (M) */
    AREA_I,     /* 输入区 / Inputs (I) */
    AREA_Q      /* 输出区 / Outputs (Q) */
} S7Area;

/* 解析结果联合体 / Parsing Result Union */
typedef struct {
    S7DataType type;   /* 当前类型 / Current data type */
    union {
        bool     b;    /* S7_BOOL              */
        int16_t  i;    /* S7_INT               */
        int32_t  d;    /* S7_DINT              */
        float    r;    /* S7_REAL              */
        uint8_t  u8;   /* S7_BYTE / S7_USINT   */
        int8_t   i8;   /* S7_SINT              */
        uint16_t u16;  /* S7_WORD / S7_UINT    */
        uint32_t u32;  /* S7_DWORD / S7_UDINT  */
        double   lr;   /* S7_LREAL             */
    } as;
} S7Value;

/* 获取类型的字节长度 / Get byte size of data type */
int s7_type_size(S7DataType t);

/* 获取类型字符串名称 / Get string name of data type */
const char *s7_type_name(S7DataType t);

/* 从字符串解析数据类型 / Parse data type from string */
int s7_type_from_str(const char *s, S7DataType *out);

/* 获取存储区字符串名称 / Get string name of memory area */
const char *s7_area_name(S7Area a);

/* 从字符串解析存储区 / Parse memory area from string */
int s7_area_from_str(const char *s, S7Area *out);

/*
 * 字节流解码为数值 / Decode byte stream to value
 * 成功返回0，失败返回-1 / Returns 0 on success, -1 on failure
 */
int s7_decode(S7DataType t, const uint8_t *buf, int bit, S7Value *out);

#endif /* S7_TYPES_H */
