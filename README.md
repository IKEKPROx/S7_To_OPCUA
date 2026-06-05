# S7ToOPCUA —— 西门子 S7 转 OPC UA 网关

本网关用于实时采集西门子 S7 系列 PLC（S7-300/400/1200/1500）的数据，并将其转换为标准的 **OPC UA** 节点，以便 SCADA / MES / 组态软件 / UAExpert 等上位机系统进行读取与集成。

本项目采用 C 语言开发，基于以下开源库构建：
- **[snap7](https://snap7.sourceforge.net/)**（libsnap7）—— 实现与 S7 PLC 的通信（客户端角色）
- **[open62541](https://www.open62541.org/)** —— 提供 OPC UA 服务（服务器角色）

*(For the English version, please scroll down to the [English Documentation](#english-documentation) section.)*

---

## 目录

1. [应用场景与价值](#1-应用场景与价值)
2. [系统架构与数据流向](#2-系统架构与数据流向)
3. [目录结构说明](#3-目录结构说明)
4. [核心模块解析](#4-核心模块解析)
5. [核心技术概念](#5-核心技术概念)
6. [环境与依赖](#6-环境与依赖)
7. [编译与构建](#7-编译与构建)
8. [配置文件详解](#8-配置文件详解)
9. [运行部署](#9-运行部署)
10. [客户端连接与验证](#10-客户端连接与验证)
11. [测试方案与覆盖范围](#11-测试方案与覆盖范围)
12. [部署注意事项（重要）](#12-部署注意事项重要)
13. [常见问题诊断与排查](#13-常见问题诊断与排查)
14. [功能特性与演进规划](#14-功能特性与演进规划)
15. [术语表](#15-术语表)

---

## 1. 应用场景与价值

由于 PLC 采用西门子私有的 **S7 通信协议**，而上位机系统通常采用国际标准 **OPC UA**，两者之间存在通信壁垒。本网关作为协议转换枢纽，实现了两者之间的无缝对接：

```text
  西门子 PLC          [ 本网关 ]                上位机
 ┌─────────┐  S7协议  ┌──────────────┐  OPC UA  ┌──────────┐
 │ DB/M/I/Q │ ───────► │ 采集 → 缓存 → │ ───────► │ UAExpert │
 │  数据    │         │   暴露为节点   │         │ SCADA/MES│
 └─────────┘         └──────────────┘         └──────────┘
```

---

## 2. 系统架构与数据流向

系统核心设计理念为**“基于互斥锁机制的缓存实现数据采集与服务分发的解耦”**：

```text
                         ┌──────────────────────────────────────┐
   真实 PLC              │              本网关进程                │            上位机
 ┌──────────┐           │                                        │         ┌──────────┐
 │ Line1_PLC│◄──S7读────┤ [轮询线程1] ──写──┐                     │         │          │
 └──────────┘           │                   ▼                     │         │  OPC UA  │
 ┌──────────┐           │              ┌─────────┐   读  ┌──────┐ │◄─OPCUA──┤  客户端  │
 │ Line2_PLC│◄──S7读────┤ [轮询线程2] ─┤ Tag缓存 ├──────►│OPC UA│ │         │          │
 └──────────┘           │              │ (加锁)  │  回调  │Server│ │         └──────────┘
                        │              └─────────┘       └──────┘ │
                        └──────────────────────────────────────┘
                            写端(多线程)        读端(服务器单线程)
```

**设计优势：**

- **独立轮询线程**：鉴于 snap7 的读取操作具有阻塞特性，为避免单一 PLC 网络延迟影响整体系统性能，系统为每台 PLC 分配独立的采集线程。
- **线程安全缓存机制（tag_cache）**：采集线程仅负责更新缓存中的实时数据，OPC UA 服务器单向从缓存读取数据。通过缓存层实现异步通信，确保 OPC UA 服务器免受并发写入冲突影响，极大提升了多线程环境下的系统稳定性。
- **动态回调读取（DataSource）**：当客户端请求节点数据时，open62541 触发回调函数，系统即时从缓存中获取当前状态值并返回，确保数据的高度实时性，避免了冗余的数据推送开销。

数据处理链路：

```text
PLC原始字节 ──①snap7读取──► s7_client解析为标准数据类型 ──②──► 写入tag_cache
                                                                  │
上位机显示数值 ◄──④OPC UA服务返回── opcua_server触发read回调 ◄──③── 从tag_cache读取
```

---

## 3. 目录结构说明

```text
S7ToOPCUA/
├── Makefile                  # 构建脚本（支持 make / make test / make clean）
├── README.md                 # 本文档
├── config/
│   └── gateway.json          # ★ 核心配置文件（配置 PLC 连接参数与采集点表）
├── include/                  # 头文件（接口声明与定义）
│   ├── s7_types.h            #   ① S7 数据解析与类型/内存区定义
│   ├── config.h              #   ② 配置解析数据结构
│   ├── s7_client.h           #   ③ snap7 通信层封装
│   ├── tag_cache.h           #   ④ 线程安全的数据缓存机制
│   ├── opcua_server.h        #   ⑤ OPC UA 服务器功能封装
│   └── poll_engine.h         #   ⑥ 数据采集轮询引擎
├── src/                      # 核心实现逻辑
│   ├── s7_types.c
│   ├── config.c
│   ├── s7_client.c
│   ├── tag_cache.c
│   ├── opcua_server.c
│   ├── poll_engine.c
│   └── main.c                #   主程序入口
├── tests/                    # 单元与集成测试模块（支持独立运行）
│   ├── test_s7_types.c
│   ├── test_config.c         + 异常配置测试用例 (bad_config.json)
│   ├── test_s7_client.c      # 基于 snap7 模拟服务器进行测试
│   ├── test_tag_cache.c      # 多线程并发读写测试
│   └── test_opcua_server.c   # 服务器端与客户端联调测试
└── third_party/
    └── snap7/                # 由源码编译的 snap7 依赖
        ├── include/snap7.h
        └── lib/libsnap7.dylib
```

---

## 4. 核心模块解析

系统采用自底向上的分层架构设计，各层仅对其底层模块产生依赖，从而实现高内聚低耦合，支持独立编译与测试验证。

### ① s7_types —— 数据解析（底层支撑，无外部依赖）

S7 协议在网络传输中采用**大端序（big-endian）原始字节流**，该模块负责将原始字节流解析为标准 C 语言数据类型。

- `s7_decode(type, buf, bit, &out)`：核心解析函数。依据配置的类型参数将字节转化为 `S7Value`。
- 支持 12 种主流数据类型：`BOOL` / `BYTE` / `SINT` / `USINT` / `INT` / `UINT` / `WORD` / `DINT` / `UDINT` / `DWORD` / `REAL` / `LREAL`（详细参考第 8 章类型映射表）。
- `S7Value` 采用带有类型标识的联合体（union）设计，保持与 OPC UA 协议的解耦，便于独立验证。

### ② config —— 配置解析（依赖 cJSON）

将 `gateway.json` 转换为内部 C 语言结构体对象（`GatewayCfg` → `PlcCfg[]` → `TagCfg[]`）。

- 提供 `config_load(path, &cfg)` 与 `config_free(&cfg)` 接口。
- 实施严格的字段完整性校验与数值范围验证：验证数据类型/区域合法性、偏移量 `start>=0`、DB 块号 `db>=1`、端口 `port` 1~65535、轮询间隔 `poll_interval_ms>0`、BOOL 类型位偏移 `bit(0~7)` 等。配置解析失败将立即终止运行，阻断异常状态传播。

### ③ s7_client —— S7 通信层（依赖 snap7）

对 snap7 客户端进行封装，实现单个 `S7Conn` 实例与单台 PLC 的映射关联。

- 核心接口：`s7_conn_create` / `connect` / `read_tag` / `disconnect` / `destroy`。
- 数据读取（`s7_conn_read_tag`）：根据节点配置，调用 `Cli_ReadArea` 获取字节数据，随后依赖 `s7_decode` 进行数值解析。
- 通信异常处理：读取失败时，连接状态将被标记为“断开”，触发上层重连逻辑。
- `S7Conn` 采用**不透明指针**设计模式，实现良好的信息隐藏与模块封装。

### ④ tag_cache —— 线程安全缓存（标准 C + pthread）

为每台 PLC 分配独立的缓存区块，针对单个数据点建立存储单元，包含：**数据值、质量标识及时间戳**。

- 采用 `pthread_mutex` 实施并发控制，确保读写操作互斥，提供数据一致性保障。
- 写入接口：`set_good`（更新有效值）/ `set_bad`（通信异常，保留历史数据更新状态）/ `set_all_bad`。
- 读取接口：`get`（获取数据快照）。
- **质量管理**：支持 `UNINIT`（未初始化）、`GOOD`（正常）、`BAD`（通信异常）。当通信异常时保留最后已知数值并标记为非优质量状态，符合工业自动化系统异常处理规范。

### ⑤ opcua_server —— OPC UA 服务层（依赖 open62541）

基于配置信息构建服务器地址空间，为每个数据点实例化 **DataSource 变量节点**。

- 地址空间层级：`Objects / <PLC名称> / <节点名称>`。
- 节点采用动态回调机制而非静态数据存储。客户端发起读取请求时触发 open62541 回调机制，系统实时从 tag_cache 获取数据，转换格式并附加质量戳、时间戳进行响应。
- 状态映射：将缓存层 `GOOD` 映射至 OPC UA `Good`，`BAD` 映射至 `BadNoCommunication`。

### ⑥ poll_engine + main —— 进程调度与启动

- `poll_engine`：为单台 PLC 创建独立线程，执行循环任务流程：`状态检测与重连 → 节点遍历读取 → 缓存更新 → 间隔休眠(poll_interval)`。跨线程终止信号采用 `atomic_int`（保障多线程内存可见性）。
- `main`：应用主干流程：读取配置 → 初始化缓存 → 启动轮询引擎 → 运行 OPC UA 主服务（阻塞主线程）→ 捕获 `Ctrl+C` 信号执行**优雅关闭**（终止服务 → 线程同步回收 → 资源释放）。

---

## 5. 核心技术概念

| 技术概念 | 核心说明 | 源码参照 |
|---|---|---|
| **大端序 (Big-endian)** | S7 数据在网络传输中高字节在前，而 PC 架构普遍为小端序，需进行字节重排解析。 | `s7_types.c` 的 `be_u16/be_u32` |
| **位级提取 (BOOL)** | BOOL 类型数据存在于特定字节的指定位，解析逻辑需使用位运算 `(byte>>bit)&1`。 | `s7_types.c` |
| **浮点数解析 (REAL)** | 将 32 位大端整数进行字节重排后，采用 `memcpy` 复制到 `float`，规避指针强转导致的未定义行为。 | `s7_types.c` |
| **不透明指针 (Opaque Pointer)** | 头文件中仅声明 `typedef struct S7Conn S7Conn;`，屏蔽内部结构，实现严格的数据封装。 | `s7_client` |
| **数据源回调 (DataSource)** | 节点值依赖客户端读取请求即时触发回调函数获取，保障数据采集的高实时性。 | `opcua_server.c` |
| **质量标识 (Quality)** | 在通信中断时保留历史值并标记 Bad 状态，以便监控系统准确掌握通信健康度。 | `tag_cache` |
| **并发隔离 (线程模型)** | 鉴于 snap7 通信存在阻塞，通过独立线程隔离不同 PLC 的通信任务，避免级联延迟。 | `poll_engine.c` |
| **原子操作 (Atomic)** | 采用 `<stdatomic.h>` 替代 `volatile` 声明跨线程控制标志，以确保内存可见性与同步正确性。 | `poll_engine.c` |
| **优雅关闭 (Graceful Shutdown)** | 进程退出时执行完整的资源回收生命周期：停止工作线程、断开网络连接、释放内存分配。 | `main.c` |

---

## 6. 环境与依赖

本项目基于 **macOS (Apple Silicon) 架构与 Apple clang** 环境构建，系统级依赖如下：

| 依赖组件 | 功能定位 | 获取方式 |
|---|---|---|
| clang / make | 核心编译工具链 | macOS 系统提供（需安装 Xcode Command Line Tools） |
| **cJSON** | JSON 配置文件解析 | 执行 `brew install cjson` |
| **open62541** | 支撑 OPC UA 服务器构建 | 执行 `brew install open62541` |
| **snap7** | S7 通信协议栈 | **详见下方编译说明**（需自行获取源码编译） |

### macOS 环境下的 snap7 编译指导（已内置于 `third_party/snap7/`）

如需在全新开发环境重建依赖库，请参照如下构建流程：

```bash
# 1. 获取源码包（推荐直接通过 SourceForge 下载直链获取 7z 压缩包）
curl -L -o /tmp/snap7.7z \
  "https://master.dl.sourceforge.net/project/snap7/1.4.2/snap7-full-1.4.2.7z?viasf=1"
brew install sevenzip && 7zz x -y /tmp/snap7.7z -o/tmp

# 2. 编译动态链接库 (移除 -lrt 选项以适配 macOS)
cd /tmp/snap7-full-1.4.2/src
clang++ -O3 -fPIC -shared -Isys -Icore -Ilib \
  sys/*.cpp core/*.cpp lib/*.cpp -lpthread -o libsnap7.dylib

# 3. 集成至工程目录
cp libsnap7.dylib  <项目>/third_party/snap7/lib/
cp ../release/Wrappers/c-cpp/snap7.h  <项目>/third_party/snap7/include/
install_name_tool -id @rpath/libsnap7.dylib  <项目>/third_party/snap7/lib/libsnap7.dylib
```

> ⚠️ 注意事项：项目集成的 `snap7.h` 包含两项兼容性补丁（**请勿覆盖**）：
> 1. 新增 `<stdbool.h>` 宏定义防冲突机制，以兼容标准 C 库。
> 2. 将文件作用域的 `const int` 定义修改为 `static const` 声明，解决多源码文件包含产生的链接重复定义冲突。

---

## 7. 编译与构建

```bash
make            # 执行标准构建，生成目标程序 -> ./gateway
make test       # 构建并执行全量单元与集成测试套件
make clean      # 清理中间构建产物与目标文件
make tsan       # （调试用）启用 ThreadSanitizer 构建，用于并发状态的数据竞争检测
```

---

## 8. 配置文件详解

系统运行主要依赖于 [`config/gateway.json`](config/gateway.json) 配置文件，实现了参数化运行与业务逻辑的解耦。

```jsonc
{
  "opcua": { "port": 4840 },          // 设定 OPC UA 服务监听端口（可选，缺省配置为 4840）
  "plcs": [                            // 定义 PLC 终端集合，支持多设备接入
    {
      "name": "Line1_PLC",            // 定义 PLC 逻辑名称，将作为 OPC UA 节点命名空间前缀（必填）
      "ip": "192.168.0.1",            // 设定 PLC 终端网络 IP 地址（必填）
      "port": 102,                    // 设定 S7 协议端口（可选，标准 S7 通信缺省配置为 102）
      "rack": 0,                      // 设定系统机架编号（可选，缺省配置为 0）
      "slot": 1,                      // 设定 CPU 模块槽位号（可选，缺省配置为 1）
      "poll_interval_ms": 200,        // 设定轮询采集周期，单位毫秒（可选，缺省配置为 200）
      "tags": [                       // 定义数据采集点位集合（必填且至少包含一项）
        { "name": "Motor1.Speed", "area": "DB", "db": 10, "start": 0,  "type": "REAL" },
        { "name": "Motor1.Run",   "area": "DB", "db": 10, "start": 4,  "bit": 0, "type": "BOOL" },
        { "name": "Motor1.Count", "area": "DB", "db": 10, "start": 6,  "type": "DINT" }
      ]
    }
  ]
}
```

### 采集点位配置规格

| 字段标识 | 语义说明 | 约束与有效值域 | 必填状态 |
|---|---|---|---|
| `name` | 数据点标识，用于生成 OPC UA 变量节点名称 | 字符串，如 `"Motor1.Speed"` | ✅ 必填 |
| `area` | S7 内存区域定义 | `DB` / `M` / `I` / `Q` | ✅ 必填 |
| `db` | 数据块 (Data Block) 序号 | 整数 ≥1（仅当 `area` 为 `DB` 时生效） | 针对 DB 必填 |
| `start` | 起始字节偏移量 | 整数 ≥0 | ✅ 必填 |
| `bit` | 位偏移量 | 0~7（**专用于 BOOL 类型解析**） | 针对 BOOL 必填 |
| `type` | 标准数据类型标识 | 参照后续类型映射表（支持 12 种基本类型） | ✅ 必填 |

### 数据类型映射表

| 标识 `type` | 对应 S7 原生数据类型 | 长度 | 映射 OPC UA 数据类型 | 典型寻址示例 |
|---|---|---|---|---|
| `BOOL`  | 布尔型 (单比特)     | 1 bit | `Boolean` | `DB10.DBX4.0` |
| `BYTE`  | 8位无符号整型       | 1 byte| `Byte` (UInt8) | `DB10.DBB0` |
| `USINT` | 8位无符号整型       | 1 byte| `Byte` (UInt8) | `DB10.DBB0` |
| `SINT`  | 8位有符号整型       | 1 byte| `SByte` (Int8) | `DB10.DBB0` |
| `WORD`  | 16位无符号整型      | 2 byte| `UInt16` | `DB10.DBW0` |
| `UINT`  | 16位无符号整型      | 2 byte| `UInt16` | `DB10.DBW0` |
| `INT`   | 16位有符号整型      | 2 byte| `Int16`  | `DB10.DBW0` |
| `DWORD` | 32位无符号整型      | 4 byte| `UInt32` | `DB10.DBD0` |
| `UDINT` | 32位无符号整型      | 4 byte| `UInt32` | `DB10.DBD0` |
| `DINT`  | 32位有符号整型      | 4 byte| `Int32`  | `DB10.DBD0` |
| `REAL`  | 32位单精度浮点型    | 4 byte| `Float`  | `DB10.DBD0` |
| `LREAL` | 64位双精度浮点型    | 8 byte| `Double` | `DB10.DBD0` |

> **配置转换范例**：针对地址 `DB10.DBX4.0`（位于数据块 DB10 内第 4 字节的第 0 位）
> 配置转换：`{ "area":"DB", "db":10, "start":4, "bit":0, "type":"BOOL" }`
>
> 针对地址 `DB10.DBD0`（位于数据块 DB10 内起始于第 0 字节的 32 位浮点数）
> 配置转换：`{ "area":"DB", "db":10, "start":0, "type":"REAL" }`

### OPC UA 命名空间生成规则

- 设备节点生成策略：`NodeId` 格式定义为 `ns=1;s=<PLC名称>`
- 变量节点生成策略：`NodeId` 格式定义为 `ns=1;s=<PLC名称>.<节点名称>`，例如 `ns=1;s=Line1_PLC.Motor1.Speed`

---

## 9. 运行部署

```bash
./gateway                       # 加载缺省配置路径 config/gateway.json 运行
./gateway /path/to/your.json    # 加载自定义绝对/相对路径配置文件运行
```

正常启动日志示例：

```text
配置已加载: config/gateway.json  (PLC 数: 1, OPC UA 端口: 4840)
[Line1_PLC] 已建立连接 192.168.0.1:102
网关服务已启动。OPC UA 端点: opc.tcp://localhost:4840   (按 Ctrl+C 停止服务)
```

当 PLC 连接异常时（如地址错误或设备未就绪），系统将自动执行重连机制并更新数据质量标识：

```text
[Line1_PLC] 通信连接异常: TCP : Connection refused，系统将于 200ms 后自动重连
```

### 本地仿真联调测试

项目工程内建 `fake_plc` 仿真模块，利用 snap7 的服务器特性虚拟出一台标准 PLC，可持续输出动态模拟数据（包含转速、电流、计数等维度）。适配的配置文件位于 `config/fake_plc.json`。

测试执行步骤（需开启三个独立终端会话并分别进入项目根目录）：

```bash
# 终端会话 1：启动本地仿真 PLC（默认绑定 1102 端口，持续提供动态数据）
make fake_plc && ./fake_plc

# 终端会话 2：启动网关进程，绑定仿真 PLC 节点
make gateway && ./gateway config/fake_plc.json

# 终端会话 3：启动终端监控面板（为 macOS 提供轻量级 UaExpert 替代方案）
make ua_monitor && ./ua_monitor config/fake_plc.json
```

---

## 10. 客户端连接与验证

建议采用专业工具 **[UaExpert](https://www.unified-automation.com/products/development-tools/uaexpert.html)** 或兼容 OPC UA 协议的客户端实施验证：

1. 确保网关进程 `./gateway` 处于运行状态。
2. 于客户端配置服务器访问端点，设定地址为：`opc.tcp://localhost:4840`（跨节点部署时，替换为主机实际 IP）。
3. 选择匿名登录验证机制。
4. 在地址空间视图中展开层级 `Objects → Line1_PLC`，将所需监测的节点拖拽至 Data Access 视图区。

---

## 11. 测试方案与覆盖范围

系统模块实施全覆盖的单元与集成测试策略，通过 `make test` 命令可触发完整自动化测试。测试要点：

- **无硬件依赖测试架构**：利用 snap7 内建的 **server 功能**构建本地隔离的“虚拟 PLC”。
- **并发安全性边界测试**：部署多读多写线程模型并执行百万次高频交互，验证无锁/带锁状态下的数据一致性。
- **端到端集成验证**：构建全功能后端服务，结合 open62541 客户端接口模拟应用层数据提取。

---

## 12. 部署注意事项（重要）

在与真实 PLC 建立通信时，常见的连接异常主要由以下因素引起，部署前务必确认：

1. **S7-1200/1500 固件配置权限**
   需于博图工程中显式授权 `PUT/GET` 访问机制（配置路径：CPU 属性 → 防护与安全 → 连接机制 → 启用“允许从远程伙伴用 PUT/GET 通信访问”）。
2. **数据块内存布局模式**
   所需访问的 DB 块必须禁用“优化的块访问”特性（配置路径：DB 属性 → 关闭“优化的块访问”）。
3. **定位参数配置正确性**：
   - 针对 S7-1200 / 1500 架构：典型参数配置为 `rack=0, slot=1`
   - 针对 S7-300 / 400 架构：典型参数配置为 `rack=0, slot=2`
4. **网络基础设施验证**：确保部署主机具备网络可达性，且防火墙等策略节点已开放 TCP 端口 102。

---

## 13. 常见问题诊断与排查

| 故障现象 | 排查与修复建议 |
|---|---|
| `Connection refused` | 验证设备 IP、执行 `ping` 测试、检查 102 端口连通性 |
| 获取数据错位或乱码 | 取消目标 DB 块的优化属性；对齐 `start`/`type` 偏移参数 |
| 报鉴权或拒绝错误 | 进入 PLC 组态启用 PUT/GET 访问控制权限 |
| 启动提示端口冲突 | 修改配置项 `opcua.port` 或强制释放 4840 端口 |

---

## 14. 功能特性与演进规划

**当前版本特性**：多设备接入支持、基于 JSON 声明式的点表配置、**纯读机制**数据采集、容错与质量反馈、进程优雅管理、12 种核心 S7 数据类型解析。

**未来规划蓝图**：
- 实现基于 OPC UA 的数据**回写（Write-back）**支持
- 引入**合并连续读指令**优化策略
- 整合安全框架体系：启用基于 X.509 证书的加密传输及用户鉴权机制
- 拓展复杂数据结构兼容：支持变长字符串、数据阵列及复杂对象结构解析
- 基于分隔符的**层级化对象模型**自动构建
- 支持配置文件参数的**动态热重载（Hot-reload）**

---

## 15. 术语表

| 专业术语 | 定义与内涵说明 |
|---|---|
| **PLC** | 可编程逻辑控制器 |
| **S7 通信协议** | 由西门子主导的闭源私有通信规范 |
| **OPC UA** | 通用数据交互通信标准 |
| **DB / M / I / Q** | S7 体系架构下划分的特定存储器区域 |
| **大端序 (Big-endian)** | 数据高位字节存放于低地址空间的机制 |
| **Tag / 采集点位** | 指代需要被采集转换的数据单元 |
| **数据质量 (Quality)** | 表征特定数据有效性的状态标量 (Good / Bad) |

<br><br><br>

---
---

# English Documentation

# S7ToOPCUA —— Siemens S7 to OPC UA Gateway

This gateway is designed to collect real-time data from Siemens S7 series PLCs (S7-300/400/1200/1500) and convert it into standard **OPC UA** nodes for reading and integration by upper-level systems such as SCADA, MES, configuration software, and UAExpert.

This project is developed in C and built upon the following open-source libraries:
- **[snap7](https://snap7.sourceforge.net/)** (libsnap7) —— Implements communication with S7 PLCs (Client role)
- **[open62541](https://www.open62541.org/)** —— Provides OPC UA services (Server role)

---

## Table of Contents

1. [Scenarios & Value](#1-scenarios--value)
2. [System Architecture & Data Flow](#2-system-architecture--data-flow)
3. [Directory Structure](#3-directory-structure)
4. [Core Modules Analysis](#4-core-modules-analysis)
5. [Core Technical Concepts](#5-core-technical-concepts)
6. [Environment & Dependencies](#6-environment--dependencies)
7. [Compilation & Build](#7-compilation--build)
8. [Configuration File Details](#8-configuration-file-details)
9. [Execution & Deployment](#9-execution--deployment)
10. [Client Connection & Verification](#10-client-connection--verification)
11. [Testing Strategy & Coverage](#11-testing-strategy--coverage)
12. [Deployment Prerequisites (Important)](#12-deployment-prerequisites-important)
13. [Troubleshooting & FAQ](#13-troubleshooting--faq)
14. [Features & Future Roadmap](#14-features--future-roadmap)
15. [Glossary](#15-glossary)

---

## 1. Scenarios & Value

Since PLCs use Siemens' proprietary **S7 communication protocol**, while upper-level systems typically use the international standard **OPC UA**, there is a communication barrier between them. This gateway acts as a protocol conversion hub to achieve seamless integration between the two:

```text
  Siemens PLC             [ This Gateway ]             Upper System
 ┌─────────┐ S7 Protocol ┌──────────────┐  OPC UA  ┌──────────┐
 │ DB/M/I/Q │ ─────────► │ Collect Data │ ───────► │ UAExpert │
 │   Data   │            │ Cache & Node │          │ SCADA/MES│
 └─────────┘             └──────────────┘          └──────────┘
```

---

## 2. System Architecture & Data Flow

The core design philosophy is **"decoupling data collection and service distribution through a mutex-based caching mechanism"**:

```text
                         ┌──────────────────────────────────────┐
      Real PLC           │             Gateway Process            │           Upper System
 ┌──────────┐           │                                        │         ┌──────────┐
 │ Line1_PLC│◄──S7 Read ┤ [Polling Thread 1] ──Write┐            │         │          │
 └──────────┘           │                         ▼              │         │  OPC UA  │
 ┌──────────┐           │              ┌─────────┐ Read ┌──────┐ │◄─OPCUA──┤  Client  │
 │ Line2_PLC│◄──S7 Read ┤ [Polling Thread 2] ─┤Tag Cache├─────►│OPC UA│ │         │          │
 └──────────┘           │              │(Mutexed)│Cbck  │Server│ │         └──────────┘
                        │              └─────────┘      └──────┘ │
                        └──────────────────────────────────────┘
                          Writers(Multi-thread)  Reader(Single-thread)
```

**Design Advantages:**

- **Independent Polling Threads**: Given that snap7 read operations are blocking, assigning independent polling threads to each PLC prevents network delays on a single device from affecting overall system performance.
- **Thread-Safe Caching Mechanism (tag_cache)**: Polling threads only update real-time data in the cache, while the OPC UA server reads from it unidirectionally. This asynchronous communication prevents concurrent write conflicts on the server, significantly boosting stability.
- **Dynamic Callback Reading (DataSource)**: When clients request node data, open62541 triggers a callback to instantly fetch the current state from the cache, ensuring high real-time performance and avoiding redundant data push overhead.

Data Processing Flow:

```text
Raw S7 Bytes ──①snap7 Read──► s7_client Parse Types ──②──► Write to tag_cache
                                                                  │
Displayed Value ◄──④OPC UA Server Return── opcua_server Trigger Callback ◄──③── Read from tag_cache
```

---

## 3. Directory Structure

```text
S7ToOPCUA/
├── Makefile                  # Build script (make / make test / make clean)
├── README.md                 # This document
├── config/
│   └── gateway.json          # ★ Core configuration file (PLC & tag setups)
├── include/                  # Header files (Declarations)
│   ├── s7_types.h            #   ① S7 data parsing and types
│   ├── config.h              #   ② Config structures
│   ├── s7_client.h           #   ③ snap7 client wrapper
│   ├── tag_cache.h           #   ④ Thread-safe cache
│   ├── opcua_server.h        #   ⑤ OPC UA server wrapper
│   └── poll_engine.h         #   ⑥ Polling engine
├── src/                      # Implementation sources
│   ├── s7_types.c
│   ├── config.c
│   ├── s7_client.c
│   ├── tag_cache.c
│   ├── opcua_server.c
│   ├── poll_engine.c
│   └── main.c                #   Main entry point
├── tests/                    # Unit & Integration tests
│   ├── test_s7_types.c
│   ├── test_config.c         + bad_config.json
│   ├── test_s7_client.c      # Fake PLC test
│   ├── test_tag_cache.c      # Concurrency test
│   └── test_opcua_server.c   # End-to-end test
└── third_party/
    └── snap7/                # snap7 compiled from source
        ├── include/snap7.h
        └── lib/libsnap7.dylib
```

---

## 4. Core Modules Analysis

The system uses a bottom-up layered architecture, where each layer only depends on the ones below it, achieving high cohesion and low coupling for independent compilation and testing.

### ① s7_types —— Data Parsing

The S7 protocol uses **big-endian raw byte streams**. This module parses them into standard C data types.

- `s7_decode(type, buf, bit, &out)`: Core parsing function.
- Supports 12 main data types: `BOOL` / `BYTE` / `SINT` / `USINT` / `INT` / `UINT` / `WORD` / `DINT` / `UDINT` / `DWORD` / `REAL` / `LREAL`.
- `S7Value` uses a tagged union to decouple from the OPC UA protocol.

### ② config —— Configuration Parsing

Parses `gateway.json` into internal C structures.

- Implements strict field validation and range checking. Fails fast if the config is invalid to prevent anomalous states.

### ③ s7_client —— S7 Client Layer

Wraps the snap7 client, mapping a single `S7Conn` instance to a single PLC.

- Reads bytes via `Cli_ReadArea` and parses them using `s7_decode`.
- Handles read failures by triggering reconnection logic.
- Implements strict encapsulation using the opaque pointer pattern.

### ④ tag_cache —— Thread-Safe Cache

Allocates independent cache blocks for each PLC, storing: **Value, Quality, and Timestamp**.

- Uses `pthread_mutex` for concurrency control.
- **Quality Management**: Supports `UNINIT`, `GOOD`, and `BAD`. Retains the last known value and marks the quality as `BAD` when communication is lost.

### ⑤ opcua_server —— OPC UA Server Layer

Builds the server address space from configuration, instantiating **DataSource variable nodes** for each point.

- Nodes use dynamic callbacks instead of static storage, ensuring real-time responses with appended quality and timestamps.

### ⑥ poll_engine + main —— Polling & Main

- `poll_engine`: Runs a dedicated thread per PLC for cyclic polling.
- `main`: Core execution flow handling initialization, thread spawning, OPC UA server blocking execution, and graceful shutdown.

---

## 5. Core Technical Concepts

| Concept | Description |
|---|---|
| **Big-endian** | S7 sends high-order bytes first, requiring byte swapping on little-endian PCs. |
| **BOOL Extraction** | BOOL values require bitwise extraction. |
| **Float Parsing** | Float extraction uses `memcpy` to avoid undefined pointer casting. |
| **Opaque Pointer** | Hides internal struct definitions for strict encapsulation. |
| **DataSource Callback** | Values are retrieved via callbacks upon client requests for real-time accuracy. |
| **Quality Flag** | Marks Bad state while retaining historical values during disconnects. |
| **Graceful Shutdown**| Cleanly shuts down threads, network connections, and memory upon exit. |

---

## 6. Environment & Dependencies

Built on **macOS (Apple Silicon) with Apple clang**. Dependencies include:

| Dependency | Purpose | Installation |
|---|---|---|
| clang / make | Toolchain | macOS Xcode Command Line Tools |
| **cJSON** | JSON Parser | `brew install cjson` |
| **open62541** | OPC UA Server | `brew install open62541` |
| **snap7** | S7 Protocol | Compile from source |

---

## 7. Compilation & Build

```bash
make            # Build executable -> ./gateway
make test       # Run all tests
make clean      # Clean build artifacts
make tsan       # Build with ThreadSanitizer for debugging
```

---

## 8. Configuration File Details

The system relies primarily on the [`config/gateway.json`](config/gateway.json) configuration file.

```jsonc
{
  "opcua": { "port": 4840 },          // OPC UA port (optional)
  "plcs": [                            // PLC list
    {
      "name": "Line1_PLC",            // PLC Name (Prefix)
      "ip": "192.168.0.1",            // PLC IP
      "port": 102,                    // S7 Port (default 102)
      "rack": 0,                      // Rack (default 0)
      "slot": 1,                      // Slot (default 1)
      "poll_interval_ms": 200,        // Polling interval ms
      "tags": [                       // Tags array
        { "name": "Motor1.Speed", "area": "DB", "db": 10, "start": 0,  "type": "REAL" },
        { "name": "Motor1.Run",   "area": "DB", "db": 10, "start": 4,  "bit": 0, "type": "BOOL" },
        { "name": "Motor1.Count", "area": "DB", "db": 10, "start": 6,  "type": "DINT" }
      ]
    }
  ]
}
```

### Data Type Mapping

Supports 12 basic types:
`BOOL`, `BYTE`, `USINT`, `SINT`, `WORD`, `UINT`, `INT`, `DWORD`, `UDINT`, `DINT`, `REAL`, `LREAL`.

> **Config Example**:
> `DB10.DBX4.0` → `{ "area":"DB", "db":10, "start":4, "bit":0, "type":"BOOL" }`
> `DB10.DBD0` → `{ "area":"DB", "db":10, "start":0, "type":"REAL" }`

### OPC UA Namespace Rules

- Device Node: `ns=1;s=<PLC_Name>`
- Variable Node: `ns=1;s=<PLC_Name>.<Tag_Name>` (e.g. `ns=1;s=Line1_PLC.Motor1.Speed`)

---

## 9. Execution & Deployment

```bash
./gateway                       # Run with default config
./gateway /path/to/your.json    # Run with custom config
```

Upon connection failure, the system automatically attempts reconnection and updates data quality:

```text
[Line1_PLC] TCP Connection refused, retrying in 200ms
```

### Local Simulation Testing

Includes a `fake_plc` module for simulation and `ua_monitor` for console monitoring.

```bash
# Terminal 1: Start Fake PLC
make fake_plc && ./fake_plc

# Terminal 2: Start Gateway
make gateway && ./gateway config/fake_plc.json

# Terminal 3: Start Monitor
make ua_monitor && ./ua_monitor config/fake_plc.json
```

---

## 10. Client Connection & Verification

Verification using **UaExpert** is recommended:

1. Ensure `./gateway` is running.
2. Set Endpoint URL to `opc.tcp://localhost:4840`
3. Login anonymously and drag nodes to Data Access View.

---

## 11. Testing Strategy & Coverage

Run `make test` for full test suites.
- **Hardware-independent testing** via `fake_plc`.
- **Concurrency safety testing** for mutex integrity.
- **End-to-end integration testing** using open62541 client.

---

## 12. Deployment Prerequisites (Important)

When connecting to real PLCs, verify:

1. **PLC Firmware Perms**: Enable `PUT/GET` communication in TIA Portal.
2. **DB Layout**: Disable "Optimized block access" for target DBs.
3. **Rack & Slot**:
   - S7-1200/1500: `rack=0, slot=1`
   - S7-300/400: `rack=0, slot=2`
4. **Network**: Allow TCP Port 102.

---

## 13. Troubleshooting & FAQ

| Issue | Solution |
|---|---|
| `Connection refused` | Check IP, ping, verify Port 102. |
| Corrupted Data | Disable DB optimization; verify start offset/type. |
| Auth Error / Denied | Enable `PUT/GET` access on PLC. |
| OPC UA Port Conflict | Change `opcua.port` or free Port 4840. |

---

## 14. Features & Future Roadmap

**Current Features**: Multi-device support, JSON configs, read-only mechanism, fault tolerance, 12 data types parsing.

**Roadmap**:
- Write-back support
- Bulk read optimization
- X.509 certificate encryption
- Hierarchical object model
- Hot-reload configurations

---

## 15. Glossary

| Term | Definition |
|---|---|
| **PLC** | Programmable Logic Controller |
| **S7 Protocol** | Siemens proprietary communication protocol |
| **OPC UA** | Unified Architecture for industrial communication |
| **DB / M / I / Q** | S7 memory areas |
| **Big-endian** | Most significant byte first |
| **Quality** | Validity state of data points (Good / Bad) |
