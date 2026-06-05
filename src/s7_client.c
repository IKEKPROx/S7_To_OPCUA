#include "s7_client.h"
#include "snap7.h"
#include <stdlib.h>
#include <string.h>

/* S7 客户端内部状态 / S7 Client Internal State */
struct S7Conn {
    const PlcCfg *cfg;        /* 指向配置信息 / Pointer to configuration */
    S7Object      client;     /* snap7 客户端对象 / snap7 client handle */
    bool          connected;  /* 内部维护连接状态 / Internally maintained connection status */
};

/* 转换本地存储区为 snap7 常量 / Convert local S7Area to snap7 constants */
static int to_snap7_area(S7Area a)
{
    switch (a) {
        case AREA_DB: return S7AreaDB;  /* 0x84 */
        case AREA_M:  return S7AreaMK;  /* 0x83 */
        case AREA_I:  return S7AreaPE;  /* 0x81 */
        case AREA_Q:  return S7AreaPA;  /* 0x82 */
    }
    return S7AreaDB;
}

S7Conn *s7_conn_create(const PlcCfg *cfg)
{
    if (!cfg) return NULL;
    S7Conn *c = calloc(1, sizeof(S7Conn));
    if (!c) return NULL;
    c->cfg = cfg;
    c->client = Cli_Create();
    if (c->client == 0) { free(c); return NULL; }  
    c->connected = false;
    return c;
}

int s7_conn_connect(S7Conn *c)
{
    if (!c) return -1;
    if (c->connected) return 0;

    /* 配置非默认端口(可选) / Configure non-default port if specified */
    if (c->cfg->port != 0 && c->cfg->port != 102) {
        uint16_t port = (uint16_t)c->cfg->port;
        Cli_SetParam(c->client, p_u16_RemotePort, &port);
    }

    /* 尝试建立连接 / Attempt to connect */
    int err = Cli_ConnectTo(c->client, c->cfg->ip, c->cfg->rack, c->cfg->slot);
    c->connected = (err == 0);
    return err;
}

bool s7_conn_is_connected(S7Conn *c)
{
    if (!c) return false;
    /* 获取实际底层连接状态 / Get actual underlying connection status */
    int connected = 0;
    if (Cli_GetConnected(c->client, &connected) == 0)
        c->connected = (connected != 0);
    return c->connected;
}

int s7_conn_read_tag(S7Conn *c, const TagCfg *tag, S7Value *out)
{
    if (!c || !tag || !out) return -1;

    /* 获取目标类型字节数 / Get target type byte size */
    int size = s7_type_size(tag->type);
    if (size <= 0) return -1;

    uint8_t buf[8];   
    if (size > (int)sizeof(buf)) return -1;

    /* 发起读取请求 / Send read request */
    int area = to_snap7_area(tag->area);
    int dbnum = (tag->area == AREA_DB) ? tag->db : 0;

    int err = Cli_ReadArea(c->client, area, dbnum, tag->start,
                           size, S7WLByte, buf);
    if (err != 0) {
        /* 读取失败标记断开 / Mark disconnected on read failure */
        c->connected = false;
        return err;
    }

    /* 数据解码转换 / Decode byte data to value */
    if (s7_decode(tag->type, buf, tag->bit, out) != 0)
        return -1;
    return 0;
}

void s7_conn_error_text(int err, char *buf, size_t size)
{
    /* 转换 snap7 错误码 / Translate snap7 error code */
    if (!buf || size == 0) return;
    Cli_ErrorText(err, buf, (int)size);
}

void s7_conn_disconnect(S7Conn *c)
{
    if (!c) return;
    if (c->connected) {
        Cli_Disconnect(c->client);
        c->connected = false;
    }
}

void s7_conn_destroy(S7Conn *c)
{
    if (!c) return;
    s7_conn_disconnect(c);
    if (c->client) Cli_Destroy(&c->client);
    free(c);
}
