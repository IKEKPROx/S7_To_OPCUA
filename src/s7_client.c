#include "s7_client.h"
#include "snap7.h"
#include <stdlib.h>
#include <string.h>

/* snap7 的 ReadMultiVars 每个请求最多 20 个变量(= snap7.h 里的 MaxVars，
   西门子 PDU 硬上限)。这里用宏而非直接用 MaxVars，是为了能开定长栈数组(避免 VLA)。 */
#define S7_MAX_VARS 20

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

int s7_conn_read_many(S7Conn *c, const TagCfg **tags, size_t n,
                      S7Value *out_values, int *out_results)
{
    if (!c || !tags || !out_values || !out_results) return -1;

    int overall = 0;  /* 只要有一批通信失败就记下其错误码 */

    /* 按 MaxVars(20) 分批，一批最多读 20 个变量 / Split into batches of MaxVars */
    for (size_t done = 0; done < n; ) {
        size_t batch = n - done;
        if (batch > S7_MAX_VARS) batch = S7_MAX_VARS;

        TS7DataItem items[S7_MAX_VARS];      /* 传给 snap7 的请求项 */
        uint8_t     bufs[S7_MAX_VARS][8];    /* 每项的接收缓冲，最大 8 字节(LREAL) */
        size_t      item2gi[S7_MAX_VARS];    /* 请求项下标 -> 全局点下标 的映射 */
        int         item_count = 0;

        /* 组装本批请求：尺寸非法的点不进请求，直接判失败 / Assemble request items */
        for (size_t k = 0; k < batch; k++) {
            size_t gi = done + k;
            const TagCfg *tag = tags[gi];
            int size = tag ? s7_type_size(tag->type) : 0;
            if (size <= 0 || size > 8) { out_results[gi] = -1; continue; }

            TS7DataItem *it = &items[item_count];
            it->Area     = to_snap7_area(tag->area);
            it->WordLen  = S7WLByte;                              /* 按字节读，与单读一致 */
            it->DBNumber = (tag->area == AREA_DB) ? tag->db : 0;
            it->Start    = tag->start;
            it->Amount   = size;
            it->Result   = 0;
            it->pdata    = bufs[item_count];
            item2gi[item_count] = gi;
            item_count++;
        }

        if (item_count > 0) {
            int fn = Cli_ReadMultiVars(c->client, items, item_count);
            if (fn != 0) {
                /* 整批失败(多半掉线)：本批入选点全判失败，并标记需重连 */
                c->connected = false;
                overall = fn;
                for (int j = 0; j < item_count; j++)
                    out_results[item2gi[j]] = fn;
            } else {
                /* 逐项看 Result：成功的解码进 out_values，失败的记下错误码 */
                for (int j = 0; j < item_count; j++) {
                    size_t gi = item2gi[j];
                    const TagCfg *tag = tags[gi];
                    if (items[j].Result == 0 &&
                        s7_decode(tag->type, bufs[j], tag->bit, &out_values[gi]) == 0)
                        out_results[gi] = 0;
                    else
                        out_results[gi] = items[j].Result ? items[j].Result : -1;
                }
            }
        }
        done += batch;
    }
    return overall;
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
