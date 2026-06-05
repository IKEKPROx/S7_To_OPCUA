#include "s7_types.h"
#include <string.h>

int s7_type_size(S7DataType t)
{
    switch (t) {
        case S7_BOOL:  return 1;  /* BOOL 占 1 字节(内含目标位) / BOOL occupies 1 byte (containing target bit) */
        case S7_BYTE:
        case S7_SINT:
        case S7_USINT: return 1;
        case S7_INT:
        case S7_WORD:
        case S7_UINT:  return 2;
        case S7_DINT:
        case S7_REAL:
        case S7_DWORD:
        case S7_UDINT: return 4;
        case S7_LREAL: return 8;
    }
    return 0;
}

const char *s7_type_name(S7DataType t)
{
    switch (t) {
        case S7_BOOL:  return "BOOL";
        case S7_INT:   return "INT";
        case S7_DINT:  return "DINT";
        case S7_REAL:  return "REAL";
        case S7_BYTE:  return "BYTE";
        case S7_SINT:  return "SINT";
        case S7_USINT: return "USINT";
        case S7_WORD:  return "WORD";
        case S7_UINT:  return "UINT";
        case S7_DWORD: return "DWORD";
        case S7_UDINT: return "UDINT";
        case S7_LREAL: return "LREAL";
    }
    return "UNKNOWN";
}

int s7_type_from_str(const char *s, S7DataType *out)
{
    if (s == NULL || out == NULL) return -1;
    if      (strcmp(s, "BOOL")  == 0) *out = S7_BOOL;
    else if (strcmp(s, "INT")   == 0) *out = S7_INT;
    else if (strcmp(s, "DINT")  == 0) *out = S7_DINT;
    else if (strcmp(s, "REAL")  == 0) *out = S7_REAL;
    else if (strcmp(s, "BYTE")  == 0) *out = S7_BYTE;
    else if (strcmp(s, "SINT")  == 0) *out = S7_SINT;
    else if (strcmp(s, "USINT") == 0) *out = S7_USINT;
    else if (strcmp(s, "WORD")  == 0) *out = S7_WORD;
    else if (strcmp(s, "UINT")  == 0) *out = S7_UINT;
    else if (strcmp(s, "DWORD") == 0) *out = S7_DWORD;
    else if (strcmp(s, "UDINT") == 0) *out = S7_UDINT;
    else if (strcmp(s, "LREAL") == 0) *out = S7_LREAL;
    else return -1;
    return 0;
}

const char *s7_area_name(S7Area a)
{
    switch (a) {
        case AREA_DB: return "DB";
        case AREA_M:  return "M";
        case AREA_I:  return "I";
        case AREA_Q:  return "Q";
    }
    return "UNKNOWN";
}

int s7_area_from_str(const char *s, S7Area *out)
{
    if (s == NULL || out == NULL) return -1;
    if      (strcmp(s, "DB") == 0) *out = AREA_DB;
    else if (strcmp(s, "M")  == 0) *out = AREA_M;
    else if (strcmp(s, "I")  == 0) *out = AREA_I;
    else if (strcmp(s, "Q")  == 0) *out = AREA_Q;
    else return -1;
    return 0;
}

/* 转换大端序 16 位整数 / Convert Big-Endian 16-bit integer */
static uint16_t be_u16(const uint8_t *b)
{
    return (uint16_t)(((uint16_t)b[0] << 8) | (uint16_t)b[1]);
}

/* 转换大端序 32 位整数 / Convert Big-Endian 32-bit integer */
static uint32_t be_u32(const uint8_t *b)
{
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
           ((uint32_t)b[2] << 8)  |  (uint32_t)b[3];
}

/* 转换大端序 64 位整数 / Convert Big-Endian 64-bit integer */
static uint64_t be_u64(const uint8_t *b)
{
    return ((uint64_t)be_u32(b) << 32) | (uint64_t)be_u32(b + 4);
}

int s7_decode(S7DataType t, const uint8_t *buf, int bit, S7Value *out)
{
    if (buf == NULL || out == NULL) return -1;
    out->type = t;

    switch (t) {
        case S7_BOOL:
            if (bit < 0 || bit > 7) return -1;
            /* 提取特定位 / Extract specific bit */
            out->as.b = ((buf[0] >> bit) & 0x01) != 0;
            return 0;

        case S7_INT:
            /* 无符号组装后转为有符号 / Assemble unsigned then cast to signed */
            out->as.i = (int16_t)be_u16(buf);
            return 0;

        case S7_DINT:
            out->as.d = (int32_t)be_u32(buf);
            return 0;

        case S7_REAL: {
            /* 转换 IEEE754 浮点数以避免强制转换未定义行为 / Use memcpy for IEEE754 float to avoid undefined behavior */
            uint32_t bits = be_u32(buf);
            float f;
            memcpy(&f, &bits, sizeof(f));
            out->as.r = f;
            return 0;
        }

        case S7_BYTE:
        case S7_USINT:
            out->as.u8 = buf[0];
            return 0;

        case S7_SINT:
            out->as.i8 = (int8_t)buf[0];
            return 0;

        case S7_WORD:
        case S7_UINT:
            out->as.u16 = be_u16(buf);
            return 0;

        case S7_DWORD:
        case S7_UDINT:
            out->as.u32 = be_u32(buf);
            return 0;

        case S7_LREAL: {
            /* 转换 64 位 IEEE754 双精度浮点数 / Convert 64-bit IEEE754 double precision float */
            uint64_t bits = be_u64(buf);
            double d;
            memcpy(&d, &bits, sizeof(d));
            out->as.lr = d;
            return 0;
        }
    }
    return -1;
}
