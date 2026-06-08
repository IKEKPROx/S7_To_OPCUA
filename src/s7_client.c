#include "s7_client.h"
#include "snap7.h"
#include <stdlib.h>
#include <string.h>

/* Internal connection handle structure / 内部连接句柄结构体 */
struct S7Conn {
    const PlcCfg *cfg;
    S7Object      client;
    bool          connected;
};

/* Map S7Area to snap7 area constants / 映射 S7Area 至 snap7 区域常量 */
static int to_snap7_area(S7Area a)
{
    switch (a) {
        case AREA_DB: return S7AreaDB;
        case AREA_M:  return S7AreaMK;
        case AREA_I:  return S7AreaPE;
        case AREA_Q:  return S7AreaPA;
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

    /* Set short timeouts to prevent blocking / 设置较短的超时时间以避免阻塞 */
    int timeout_ms = 1500;
    Cli_SetParam(c->client, p_i32_RecvTimeout, &timeout_ms);
    Cli_SetParam(c->client, p_i32_SendTimeout, &timeout_ms);

    c->connected = false;
    return c;
}

int s7_conn_connect(S7Conn *c)
{
    if (!c) return -1;
    if (c->connected) return 0;

    /* Apply custom port if specified / 若指定则应用自定义端口 */
    if (c->cfg->port != 0 && c->cfg->port != 102) {
        uint16_t port = (uint16_t)c->cfg->port;
        Cli_SetParam(c->client, p_u16_RemotePort, &port);
    }

    /* Connect to PLC / 连接至 PLC */
    int err = Cli_ConnectTo(c->client, c->cfg->ip, c->cfg->rack, c->cfg->slot);
    c->connected = (err == 0);
    return err;
}

bool s7_conn_is_connected(S7Conn *c)
{
    if (!c) return false;
    /* Sync status with underlying library / 同步底层库状态 */
    int connected = 0;
    if (Cli_GetConnected(c->client, &connected) == 0)
        c->connected = (connected != 0);
    return c->connected;
}

int s7_conn_read_tag(S7Conn *c, const TagCfg *tag, S7Value *out)
{
    if (!c || !tag || !out) return -1;

    /* Get required byte size / 获取所需字节数 */
    int size = s7_type_size(tag->type);
    if (size <= 0) return -1;

    uint8_t buf[8];
    if (size > (int)sizeof(buf)) return -1;

    /* Execute read operation / 执行读取操作 */
    int area = to_snap7_area(tag->area);
    int dbnum = (tag->area == AREA_DB) ? tag->db : 0;

    int err = Cli_ReadArea(c->client, area, dbnum, tag->start,
                           size, S7WLByte, buf);
    if (err != 0) {
        /* Update status on read failure / 读取失败时更新状态 */
        c->connected = false;
        return err;
    }

    /* Decode byte stream / 解码字节流 */
    if (s7_decode(tag->type, buf, tag->bit, out) != 0)
        return -1;
    return 0;
}

void s7_conn_error_text(int err, char *buf, size_t size)
{
    /* Fetch error description / 获取错误描述 */
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
