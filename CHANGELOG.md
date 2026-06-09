# 更新日志 · Changelog

本项目遵循语义化版本（SemVer）。 · This project follows Semantic Versioning (SemVer).

## v0.1.2 — 2026-06-09

本版重点：**批量读（ReadMultiVars 攒批刷新）**——把“一个点一趟 PLC”压成“一趟读多个点”，大幅降低多点位场景的 PLC 往返次数。

> Highlights: **Batch reading (ReadMultiVars coalescing)** — collapses "one PLC round-trip per point" into "many points per round-trip", greatly cutting PLC round-trips for multi-point workloads.

### ✨ 新增 · Added

- **批量读** `s7_conn_read_many()`：用 snap7 `Cli_ReadMultiVars` 一次请求读多个变量，内部自动按 `MaxVars=20`（S7 协议每请求变量数硬上限）分批，逐项返回成败。
  · **Batch read** `s7_conn_read_many()`: reads multiple variables per request via snap7 `Cli_ReadMultiVars`, auto-splitting by `MaxVars=20` (S7 per-request hard limit), with per-item results.
- **按需攒批刷新**：OPC UA 读任一过期节点时，网关把**同一台 PLC 当前所有过期点**一次性批量读回写入缓存，其余点直接命中缓存。读 N 个点的 PLC 往返从 N 趟降到 `ceil(N/20)` 趟。
  · **On-demand batch refresh**: reading any stale node makes the gateway bulk-read **all currently-stale tags of that PLC** at once into cache; other nodes hit the cache. PLC round-trips for N points drop from N to `ceil(N/20)`.
- **配置开关** `collection.batch_read`（默认 `1` 开启）：填 `0` 可关回 0.1.1 的逐点读行为。
  · **Config switch** `collection.batch_read` (default `1`): set `0` to fall back to 0.1.1 per-point reads.
- **链路冒烟脚本** `scripts/bench_batch.sh`：一键起 `fake_plc + gateway + ua_monitor` 验证整条链路。
  · **Link smoke script** `scripts/bench_batch.sh`: brings up `fake_plc + gateway + ua_monitor` to verify the whole pipeline.

### ✅ 兼容性 / 质量 · Compatibility / Quality

- 纯追加、向后兼容：`batch_read` 默认开但对外行为与 0.1.1 一致（只是更快）；旧配置无需改动；`s7_conn_read_tag`、NodeId、TTL、12 种类型、配置格式全不变。
  · Additive and backward-compatible: `batch_read` defaults on but external behavior matches 0.1.1 (just faster); legacy configs need no change.
- 全部 **95** 个单元/集成测试通过（从 71 增加）：新增 s7_client 跨批读、config 开关、OPC UA 场景 B（25 点跨 2 批批量刷新）与场景 C（关闭后运行时退回逐点）。
  · All **95** tests pass (up from 71): added s7_client cross-batch read, config switch, and OPC UA scenario B (25-point 2-batch refresh) / scenario C (runtime fallback when off).
- 真链路实测（`fake_plc + gateway + ua_monitor`，11 点）：`batch_read=1` 每轮 **1 趟** `ReadMultiVars`；`batch_read=0` 每轮 **11 趟** `ReadArea`。
  · Live test (11 points): `batch_read=1` → **1** `ReadMultiVars` per cycle; `=0` → **11** `ReadArea` per cycle.

### 📦 镜像用法 · Image Usage

```bash
docker pull impxssive/s7-opcua:0.1.2

# 直接用 JSON 配置 · Use a JSON config
docker run --rm -p 4840:4840 -v ./config:/app/config \
  impxssive/s7-opcua:0.1.2 config/gateway.json
```

---

## v0.1.1 — 2026-06-08

本版重点：**Excel 点表导入** 与 **按点表 NodeID 暴露节点**，并把点表转换整合进 Docker 镜像。

> Highlights: **Excel point-table import** and **exposing nodes by point-table NodeID**, plus integrating the converter into the Docker image.

### ✨ 新增 · Added

- **Excel 点表导入**（独立脚本 `tools/xlsx_to_config.py`，不依赖也不修改 C 网关）
  · **Excel point-table import** (standalone `tools/xlsx_to_config.py`; does not depend on or modify the C gateway)
  - 支持中文工业点表：自动解析西门子地址（`DB1.DBW2` / `DB1.DBX8.0` / `MW10`…）、映射数据类型（`float32→REAL` / `float64→LREAL` / `int16→INT`…）。
    · Handles Chinese industrial tables: parses Siemens addresses (`DB1.DBW2` / `DB1.DBX8.0` / `MW10`…) and maps data types (`float32→REAL` / `float64→LREAL` / `int16→INT`…).
  - 同时支持显式列点表；两种格式按表头自动识别（`--format` 可强制）。连接信息走命令行参数（一表一 PLC）。
    · Also supports explicit-column tables; formats are auto-detected by headers (`--format` to force). Connection info via CLI flags (one table = one PLC).
- **按点表 NodeID 暴露节点**（C 网关，可选、向后兼容）
  · **Expose nodes by point-table NodeID** (C gateway; optional, backward-compatible)
  - 配置新增可选 `node_id`：网关据此用指定 NodeId（如 `ns=2;s=[1001001]`）暴露节点、自动注册命名空间；不填则回退自动生成 `ns=1;s=PLC名.点名`，与旧配置一致。
    · New optional `node_id` field: the gateway serves the node under that NodeId (e.g. `ns=2;s=[1001001]`) and auto-registers the namespace; if absent, falls back to auto-generated `ns=1;s=PLCName.TagName`, identical to legacy behavior.
  - 监控工具 `ua_monitor` 同步支持按 `node_id` 读取。
    · The `ua_monitor` tool also reads by `node_id`.
- **Docker 镜像内置点表转换**：打包 `python3 + openpyxl + 脚本`；入口支持直接给 JSON，或 `--xlsx <点表>` 容器内自动转换后启动。
  · **In-image point-table conversion**: bundles `python3 + openpyxl + the script`; the entrypoint accepts either a JSON config, or `--xlsx <table>` to convert inside the container then start.

### 🐛 修复 / 改进 · Fixed / Improved

- 转换器：`poll_interval_ms` 跨行一致性检查；`--sheet` 名写错给清晰提示；退出码统一（`0` 成功 / `1` 内容有错 / `2` 无法处理）。
  · Converter: cross-row `poll_interval_ms` consistency check; clear message on a wrong `--sheet` name; unified exit codes (`0` ok / `1` content error / `2` cannot process).
- 文档：README（中/英）新增「Excel 点表导入与中文点表」章节，含完整列映射、地址解析、类型映射表。
  · Docs: READMEs (CN/EN) gained an "Excel Point-Table Import" chapter with full column mapping, address parsing, and type mapping tables.

### ✅ 兼容性 / 质量 · Compatibility / Quality

- C 网关改动均为纯追加、向后兼容：无 `node_id` 的旧 JSON 配置行为不变。
  · All C gateway changes are additive and backward-compatible: legacy JSON configs without `node_id` behave exactly as before.
- 全部 71 个单元/集成测试通过（含新增的"按自定义 NodeId 读取"用例）。
  · All 71 unit/integration tests pass (including the new "read by custom NodeId" case).
- 多架构镜像：`linux/amd64` + `linux/arm64`。
  · Multi-arch image: `linux/amd64` + `linux/arm64`.

### 📦 镜像用法 · Image Usage

```bash
docker pull impxssive/s7-opcua:0.1.1

# 直接用 JSON 配置 · Use a JSON config
docker run --rm -p 4840:4840 -v ./config:/app/config \
  impxssive/s7-opcua:0.1.1 config/gateway.json

# 或：喂 Excel 点表，容器内自动转换后启动 · Or: feed an Excel table, auto-converted in-container
docker run --rm -p 4840:4840 -v ./config:/app/config \
  impxssive/s7-opcua:0.1.1 --xlsx /app/config/points.xlsx --ip 192.168.0.10 --plc-name 炉区PLC
```

---

## v0.1.0 — 初始版本 · Initial Release

- S7（snap7 / ISO-on-TCP）→ OPC UA（open62541）只读协议网关。
  · Read-only S7 (snap7 / ISO-on-TCP) → OPC UA (open62541) protocol gateway.
- 按需采集 + 缓存 TTL：无客户端请求时不读 PLC；缓存过期才穿透读取。
  · On-demand polling + cache TTL: no PLC reads without a client request; passthrough read only on cache expiry.
- 支持 12 种数据类型、多 PLC、JSON 点表配置。
  · 12 data types, multiple PLCs, JSON point-table config.
- 配套工具：`fake_plc`（假 PLC 本地联调）、`ua_monitor`（终端版 OPC UA 监控面板）。
  · Tooling: `fake_plc` (mock PLC for local testing), `ua_monitor` (terminal OPC UA dashboard).
- 多架构 Docker 镜像（`linux/amd64` + `linux/arm64`）。
  · Multi-arch Docker image (`linux/amd64` + `linux/arm64`).
