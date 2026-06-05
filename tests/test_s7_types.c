/*
 * test_s7_types —— 不连 PLC，喂写死的字节，验证大端解析对不对。
 * 编译：见同目录 Makefile，或：
 *   clang -I../include test_s7_types.c ../src/s7_types.c -o test_s7_types
 */
#include "s7_types.h"
#include <stdio.h>
#include <math.h>

static int g_pass = 0, g_fail = 0;

static void check(int cond, const char *msg)
{
    if (cond) { g_pass++; printf("  [PASS] %s\n", msg); }
    else      { g_fail++; printf("  [FAIL] %s\n", msg); }
}

int main(void)
{
    S7Value v;

    printf("== INT (2字节大端, 有符号) ==\n");
    /* 0x012C = 300 */
    { uint8_t b[] = {0x01, 0x2C}; s7_decode(S7_INT, b, 0, &v);
      check(v.as.i == 300, "0x012C -> 300"); }
    /* 0xFFFF = -1 (补码) */
    { uint8_t b[] = {0xFF, 0xFF}; s7_decode(S7_INT, b, 0, &v);
      check(v.as.i == -1, "0xFFFF -> -1"); }

    printf("== DINT (4字节大端, 有符号) ==\n");
    /* 0x00000100 = 256 */
    { uint8_t b[] = {0x00, 0x00, 0x01, 0x00}; s7_decode(S7_DINT, b, 0, &v);
      check(v.as.d == 256, "0x00000100 -> 256"); }
    /* 0xFFFFFFFF = -1 */
    { uint8_t b[] = {0xFF, 0xFF, 0xFF, 0xFF}; s7_decode(S7_DINT, b, 0, &v);
      check(v.as.d == -1, "0xFFFFFFFF -> -1"); }

    printf("== REAL (4字节大端 IEEE754) ==\n");
    /* 1.5f 的 IEEE754 = 0x3FC00000 */
    { uint8_t b[] = {0x3F, 0xC0, 0x00, 0x00}; s7_decode(S7_REAL, b, 0, &v);
      check(fabsf(v.as.r - 1.5f) < 1e-6f, "0x3FC00000 -> 1.5"); }
    /* -123.456f 的 IEEE754 = 0xC2F6E979 */
    { uint8_t b[] = {0xC2, 0xF6, 0xE9, 0x79}; s7_decode(S7_REAL, b, 0, &v);
      check(fabsf(v.as.r - (-123.456f)) < 1e-3f, "0xC2F6E979 -> -123.456"); }

    printf("== BOOL (从某字节取某位) ==\n");
    /* 字节 0x04 = 二进制 00000100，第 2 位是 1 */
    { uint8_t b[] = {0x04}; s7_decode(S7_BOOL, b, 2, &v);
      check(v.as.b == true,  "0x04 bit2 -> true"); }
    { uint8_t b[] = {0x04}; s7_decode(S7_BOOL, b, 0, &v);
      check(v.as.b == false, "0x04 bit0 -> false"); }

    printf("== 扩展类型 ==\n");
    /* BYTE/USINT: 无符号 8 位，0xFF = 255 */
    { uint8_t b[] = {0xFF}; s7_decode(S7_BYTE, b, 0, &v);
      check(v.as.u8 == 255, "BYTE 0xFF -> 255"); }
    /* SINT: 有符号 8 位，0xFF = -1，0x80 = -128 */
    { uint8_t b[] = {0xFF}; s7_decode(S7_SINT, b, 0, &v);
      check(v.as.i8 == -1, "SINT 0xFF -> -1"); }
    { uint8_t b[] = {0x80}; s7_decode(S7_SINT, b, 0, &v);
      check(v.as.i8 == -128, "SINT 0x80 -> -128"); }
    /* WORD/UINT: 无符号 16 位大端，0xFFFF = 65535 */
    { uint8_t b[] = {0xFF, 0xFF}; s7_decode(S7_WORD, b, 0, &v);
      check(v.as.u16 == 65535, "WORD 0xFFFF -> 65535"); }
    { uint8_t b[] = {0x01, 0x00}; s7_decode(S7_UINT, b, 0, &v);
      check(v.as.u16 == 256, "UINT 0x0100 -> 256"); }
    /* DWORD/UDINT: 无符号 32 位大端 */
    { uint8_t b[] = {0xFF,0xFF,0xFF,0xFF}; s7_decode(S7_DWORD, b, 0, &v);
      check(v.as.u32 == 4294967295u, "DWORD 0xFFFFFFFF -> 4294967295"); }
    /* LREAL: 8 字节大端双精度，1.5 = 0x3FF8000000000000 */
    { uint8_t b[] = {0x3F,0xF8,0,0,0,0,0,0}; s7_decode(S7_LREAL, b, 0, &v);
      check(fabs(v.as.lr - 1.5) < 1e-12, "LREAL -> 1.5"); }
    { uint8_t b[] = {0xC0,0x04,0,0,0,0,0,0}; s7_decode(S7_LREAL, b, 0, &v);
      check(fabs(v.as.lr - (-2.5)) < 1e-12, "LREAL -> -2.5"); }
    /* 类型大小 */
    check(s7_type_size(S7_BYTE)==1 && s7_type_size(S7_WORD)==2
          && s7_type_size(S7_LREAL)==8, "BYTE/WORD/LREAL 大小 = 1/2/8");

    printf("== 辅助函数 ==\n");
    { S7DataType t; check(s7_type_from_str("REAL", &t) == 0 && t == S7_REAL,
                          "\"REAL\" -> S7_REAL"); }
    check(s7_type_from_str("WTF", &v.type) == -1, "未知类型字符串返回 -1");
    check(s7_type_size(S7_DINT) == 4, "DINT 占 4 字节");

    printf("\n结果: %d 通过, %d 失败\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
