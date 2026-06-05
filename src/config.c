#include "config.h"
#include <cjson/cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 读取文件全部内容 / Read entire file content */
static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "config: 打不开文件 / Cannot open file %s\n", path); return NULL; }

    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return NULL; }

    char *buf = malloc((size_t)n + 1);
    if (!buf) { fclose(f); return NULL; }

    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    buf[got] = '\0';
    return buf;
}

/* 提取必填字符串字段 / Extract required string field */
static int get_str(const cJSON *obj, const char *key, char *dst, size_t dstsz)
{
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsString(it) || it->valuestring == NULL) {
        fprintf(stderr, "config: 缺少字符串字段 / Missing string field \"%s\"\n", key);
        return -1;
    }
    if (strlen(it->valuestring) >= dstsz) {
        fprintf(stderr, "config: 字段太长 / Field too long \"%s\"\n", key);
        return -1;
    }
    strcpy(dst, it->valuestring);
    return 0;
}

/* 提取整数字段（支持默认值） / Extract integer field (supports default value) */
static int get_int(const cJSON *obj, const char *key, int required,
                   int defval, int *out)
{
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!it) {
        if (required) { fprintf(stderr, "config: 缺少字段 / Missing field \"%s\"\n", key); return -1; }
        *out = defval;
        return 0;
    }
    if (!cJSON_IsNumber(it)) {
        fprintf(stderr, "config: 字段非数字 / Field not a number \"%s\"\n", key);
        return -1;
    }
    *out = it->valueint;
    return 0;
}

/* 解析单个点位配置 / Parse single tag configuration */
static int parse_tag(const cJSON *jt, TagCfg *tag)
{
    char area_str[8], type_str[8];

    if (get_str(jt, "name", tag->name, sizeof(tag->name)) != 0) return -1;
    if (get_str(jt, "area", area_str, sizeof(area_str)) != 0)   return -1;
    if (get_str(jt, "type", type_str, sizeof(type_str)) != 0)   return -1;

    if (s7_area_from_str(area_str, &tag->area) != 0) {
        fprintf(stderr, "config: tag \"%s\" area 非法/invalid: %s\n", tag->name, area_str);
        return -1;
    }
    if (s7_type_from_str(type_str, &tag->type) != 0) {
        fprintf(stderr, "config: tag \"%s\" type 非法/invalid: %s\n", tag->name, type_str);
        return -1;
    }

    /* DB 必须指定编号 / DB area requires db number */
    int db_required = (tag->area == AREA_DB) ? 1 : 0;
    if (get_int(jt, "db", db_required, 0, &tag->db) != 0)   return -1;
    if (get_int(jt, "start", 1, 0, &tag->start) != 0)       return -1;

    /* 范围校验 / Range validation */
    if (tag->start < 0) {
        fprintf(stderr, "config: tag \"%s\" start 不能为负/cannot be negative: %d\n", tag->name, tag->start);
        return -1;
    }
    if (tag->area == AREA_DB && tag->db < 1) {
        fprintf(stderr, "config: tag \"%s\" db 必须/must be >=1: %d\n", tag->name, tag->db);
        return -1;
    }

    /* BOOL 需指定 bit 偏移 / BOOL requires bit offset */
    if (get_int(jt, "bit", 0, -1, &tag->bit) != 0)          return -1;
    if (tag->type == S7_BOOL) {
        if (tag->bit < 0 || tag->bit > 7) {
            fprintf(stderr, "config: BOOL tag \"%s\" 需要/requires bit(0~7)\n", tag->name);
            return -1;
        }
    }
    return 0;
}

/* 解析单台 PLC 配置 / Parse single PLC configuration */
static int parse_plc(const cJSON *jp, PlcCfg *plc)
{
    if (get_str(jp, "name", plc->name, sizeof(plc->name)) != 0) return -1;
    if (get_str(jp, "ip",   plc->ip,   sizeof(plc->ip)) != 0)   return -1;
    if (get_int(jp, "port", 0, 102, &plc->port) != 0)           return -1;
    if (get_int(jp, "rack", 0, 0, &plc->rack) != 0)             return -1;
    if (get_int(jp, "slot", 0, 1, &plc->slot) != 0)             return -1;
    if (get_int(jp, "poll_interval_ms", 0, 200, &plc->poll_interval_ms) != 0) return -1;

    /* 参数范围校验 / Parameter range validation */
    if (plc->port < 1 || plc->port > 65535) {
        fprintf(stderr, "config: PLC \"%s\" port 非法/invalid (1~65535): %d\n", plc->name, plc->port);
        return -1;
    }
    if (plc->poll_interval_ms <= 0) {
        fprintf(stderr, "config: PLC \"%s\" poll_interval_ms 必须/must be >0: %d\n",
                plc->name, plc->poll_interval_ms);
        return -1;
    }
    if (plc->rack < 0 || plc->slot < 0) {
        fprintf(stderr, "config: PLC \"%s\" rack/slot 不能为负/cannot be negative\n", plc->name);
        return -1;
    }

    const cJSON *jtags = cJSON_GetObjectItemCaseSensitive(jp, "tags");
    if (!cJSON_IsArray(jtags)) {
        fprintf(stderr, "config: PLC \"%s\" 缺少 tags 数组 / missing tags array\n", plc->name);
        return -1;
    }
    int n = cJSON_GetArraySize(jtags);
    if (n <= 0) {
        fprintf(stderr, "config: PLC \"%s\" tags 为空 / empty tags\n", plc->name);
        return -1;
    }

    plc->tags = calloc((size_t)n, sizeof(TagCfg));
    if (!plc->tags) return -1;
    plc->tag_count = (size_t)n;

    size_t i = 0;
    const cJSON *jt;
    cJSON_ArrayForEach(jt, jtags) {
        if (parse_tag(jt, &plc->tags[i]) != 0) return -1;  
        i++;
    }
    return 0;
}

int config_load(const char *path, GatewayCfg *out)
{
    memset(out, 0, sizeof(*out));

    char *text = read_file(path);
    if (!text) return -1;

    cJSON *root = cJSON_Parse(text);
    free(text);
    if (!root) {
        fprintf(stderr, "config: JSON 解析失败 / JSON parse failed (near %s)\n",
                cJSON_GetErrorPtr() ? cJSON_GetErrorPtr() : "?");
        return -1;
    }

    int rc = -1;  

    /* 可选 OPC UA 端口配置 / Optional OPC UA port config */
    const cJSON *jopc = cJSON_GetObjectItemCaseSensitive(root, "opcua");
    out->opcua_port = 4840;
    if (jopc) {
        if (get_int(jopc, "port", 0, 4840, &out->opcua_port) != 0) goto done;
    }

    const cJSON *jplcs = cJSON_GetObjectItemCaseSensitive(root, "plcs");
    if (!cJSON_IsArray(jplcs)) {
        fprintf(stderr, "config: 缺少 plcs 数组 / missing plcs array\n");
        goto done;
    }
    int n = cJSON_GetArraySize(jplcs);
    if (n <= 0) {
        fprintf(stderr, "config: plcs 为空 / plcs is empty\n");
        goto done;
    }

    out->plcs = calloc((size_t)n, sizeof(PlcCfg));
    if (!out->plcs) goto done;
    out->plc_count = (size_t)n;

    size_t i = 0;
    const cJSON *jp;
    cJSON_ArrayForEach(jp, jplcs) {
        if (parse_plc(jp, &out->plcs[i]) != 0) goto done;
        i++;
    }

    rc = 0;  /* 成功 / Success */

done:
    cJSON_Delete(root);
    if (rc != 0) config_free(out);   /* 失败清理内存 / Cleanup on failure */
    return rc;
}

void config_free(GatewayCfg *cfg)
{
    if (!cfg) return;
    if (cfg->plcs) {
        for (size_t i = 0; i < cfg->plc_count; i++)
            free(cfg->plcs[i].tags);   
        free(cfg->plcs);
    }
    memset(cfg, 0, sizeof(*cfg));
}
