# S7_to_OPCUA — 西门子 S7 转 OPC UA 协议网关 · Siemens S7 → OPC UA Protocol Gateway

把西门子 S7 PLC（snap7 / ISO-on-TCP）的数据转换成标准 **OPC UA** 暴露出去。纯 C 实现（基于 open62541），轻量、**按需采集保护 PLC**，并支持用 **Excel 点表**一键生成配置。

> Bridges Siemens S7 PLC data (snap7 / ISO-on-TCP) to standard **OPC UA**. Lightweight, pure C (built on open62541), with **on-demand polling that protects the PLC** and **one-click config generation from Excel point-tables**.

## ✨ 特性 · Features

- 🔌 **S7 → OPC UA 只读网关**：支持多 PLC、12 种数据类型（BOOL / INT / DINT / REAL / WORD / DWORD / LREAL …）。
  · **Read-only S7 → OPC UA gateway**: multiple PLCs, 12 data types (BOOL / INT / DINT / REAL / WORD / DWORD / LREAL …).
- ⏳ **按需采集 + 缓存 TTL**：没有客户端请求时**不碰 PLC**；缓存过期才穿透读取。
  · **On-demand polling + cache TTL**: never reads the PLC unless a client asks and the cache has expired.
- 📋 **Excel 点表导入**：支持现场常见的中文工业点表（西门子地址 `DB1.DBW2`、类型 `float32`…），自动转成网关配置。
  · **Excel point-table import**: handles Chinese industrial point-tables (Siemens addresses like `DB1.DBW2`, types like `float32`…), auto-converted into gateway config.
- 🏷️ **按点表 NodeID 暴露**：节点可直接用点表里的 `ns=2;s=[…]` 暴露，下游 SCADA/上位机无缝替换。
  · **Expose nodes by point-table NodeID**: nodes can be served under the table's own `ns=2;s=[…]`, a drop-in for existing SCADA/HMI.
- 🐳 **多架构镜像**：`linux/amd64` + `linux/arm64`（x86 服务器 / ARM 工控机 / Apple Silicon 通吃）。
  · **Multi-arch image**: `linux/amd64` + `linux/arm64` (x86 servers / ARM industrial PCs / Apple Silicon).

## 🚀 快速开始 · Quick Start

```bash
docker pull impxssive/s7-opcua:0.1.1
```

```bash
# 方式①：直接用 JSON 配置 · Mode 1: use a JSON config directly
docker run --rm -p 4840:4840 -v ./config:/app/config \
  impxssive/s7-opcua:0.1.1 config/gateway.json

# 方式②：喂 Excel 点表，容器内自动转换后启动
#        Mode 2: feed an Excel table; it is converted inside the container, then the gateway starts
docker run --rm -p 4840:4840 -v ./config:/app/config \
  impxssive/s7-opcua:0.1.1 --xlsx /app/config/points.xlsx --ip 192.168.0.10 --plc-name 炉区PLC
```

启动后用任意 OPC UA 客户端连 `opc.tcp://<主机IP>:4840`。
· Then connect any OPC UA client to `opc.tcp://<host-ip>:4840`.

## ⚙️ 配置要点 · Configuration Notes

- 用 `-v` 把你的配置目录挂到容器的 `/app/config`。
  · Mount your config directory to `/app/config` via `-v`.
- PLC 侧：开启 **PUT/GET**、DB 关闭**"优化的块访问"**、`rack/slot` 配对正确、网络可达。
  · On the PLC: enable **PUT/GET**, disable **"Optimized block access"** on DBs, match `rack/slot`, ensure network reachability.
- 端口：OPC UA 对外 `4840`；网关向 PLC 出站 `102`。
  · Ports: OPC UA exposed on `4840`; the gateway dials out to the PLC on `102`.
- `--xlsx` 后第一个是点表路径，其余参数（`--ip`/`--port`/`--plc-name`/`--opcua-port`/`--cache-ttl-ms` 等）转发给转换脚本；中文点表必须带 `--ip`。
  · After `--xlsx` comes the table path; remaining args (`--ip`/`--port`/`--plc-name`/`--opcua-port`/`--cache-ttl-ms`…) are forwarded to the converter; Chinese tables must include `--ip`.

## 🏷️ 标签 · Tags

- `0.1.1` — 当前版本：Excel 点表导入 + 按点表 NodeID 暴露。
  · Current release: Excel point-table import + expose-by-table-NodeID.
- 生产建议用**明确版本号**（如 `0.1.1`）而非 `latest`，保证可复现。
  · In production, prefer a **pinned version** (e.g. `0.1.1`) over `latest` for reproducibility.

详细文档（点表列映射、地址解析、类型映射、源码构建等）见项目 README。
· Full docs (column mapping, address parsing, type mapping, building from source) are in the project README.
