#ifndef S7_TYPES_H
#define S7_TYPES_H

/*
 * S7 Data Type Decoding / S7 数据类型解析
 * 
 * Parses big-endian byte sequences from S7 PLCs into standard C data types.
 * 负责将 S7 PLC 的大端序 (big-endian) 字节流解析为标准 C 数据类型。
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/*
 * Supported S7 Data Types / 支持的 S7 数据类型
 */
typedef enum {
    S7_BOOL,    /* 1 位   -> bool                  */
    S7_INT,     /* 2 字节 -> int16_t   (有符号)     */
    S7_DINT,    /* 4 字节 -> int32_t   (有符号)     */
    S7_REAL,    /* 4 字节 -> float     (IEEE754)    */
    S7_BYTE,    /* 1 字节 -> uint8_t   (无符号)     */
    S7_SINT,    /* 1 字节 -> int8_t    (有符号)     */
    S7_USINT,   /* 1 字节 -> uint8_t   (无符号，等同 BYTE) */
    S7_WORD,    /* 2 字节 -> uint16_t  (无符号)     */
    S7_UINT,    /* 2 字节 -> uint16_t  (无符号)     */
    S7_DWORD,   /* 4 字节 -> uint32_t  (无符号)     */
    S7_UDINT,   /* 4 字节 -> uint32_t  (无符号)     */
    S7_LREAL    /* 8 字节 -> double    (IEEE754双精度) */
} S7DataType;

/*
 * S7 Memory Areas / S7 存储区域
 */
typedef enum {
    AREA_DB,
    AREA_M,
    AREA_I,
    AREA_Q
} S7Area;

/*
 * Decoded S7 Value / 解析后的 S7 数据值
 */
typedef struct {
    S7DataType type;
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

/*
 * Get the byte size of a given S7 type / 获取 S7 类型占据的字节数
 */
int s7_type_size(S7DataType t);

int s7_type_from_str(const char *s, S7DataType *out);

const char *s7_area_name(S7Area a);
int s7_area_from_str(const char *s, S7Area *out);

/*
 * Decode byte array into S7Value / 将字节数组解码为 S7Value
 */
int s7_decode(S7DataType t, const uint8_t *buf, int bit, S7Value *out);

#endif /* S7_TYPES_H */
