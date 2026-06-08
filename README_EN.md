# S7_to_OpenUA: Siemens S7 to OPC UA Protocol Gateway

![version](https://img.shields.io/badge/version-0.1.1-blue) [![Docker Hub](https://img.shields.io/badge/Docker%20Hub-impxssive%2Fs7--opcua-2496ED?logo=docker&logoColor=white)](https://hub.docker.com/r/impxssive/s7-opcua) ![platforms](https://img.shields.io/badge/platforms-linux%2Famd64%20%7C%20arm64-555) ![C11](https://img.shields.io/badge/C-11-00599C?logo=c&logoColor=white)

This project is an industrial communication gateway developed in C. It is designed for efficient data acquisition from Siemens S7 series PLCs (including S7-300/400/1200/1500) and converting the data into standard **OPC UA** nodes. This facilitates seamless data access for SCADA, MES, and various OPC UA client systems (e.g., UaExpert).

The underlying architecture relies on two major open-source components:
- **[snap7](https://snap7.sourceforge.net/)** (libsnap7): Provides the S7 communication protocol client functionality.
- **[open62541](https://www.open62541.org/)**: Provides the OPC UA server functionality.

> **Project Note**: This is the initial release (primarily supporting read-only mode), offering both educational and engineering value. The source code features comprehensive comments covering core concepts such as industrial protocol translation, endianness conversion, multi-thread concurrency management, and callback-driven server architecture.

---

## Table of Contents

1. [Background and Core Features](#1-background-and-core-features)
2. [Architecture and Data Flow](#2-architecture-and-data-flow)
3. [Directory Structure](#3-directory-structure)
4. [Core Modules Overview](#4-core-modules-overview)
5. [Key Technical Concepts](#5-key-technical-concepts)
6. [Environment and Dependencies](#6-environment-and-dependencies)
7. [Build and Compile](#7-build-and-compile)
8. [Configuration Parameters](#8-configuration-parameters)
9. [Running Guide](#9-running-guide)
10. [System Verification and Client Connection](#10-system-verification-and-client-connection)
11. [Unit and Integration Testing](#11-unit-and-integration-testing)
12. [Hardware Integration Guide (Important)](#12-hardware-integration-guide-important)
13. [Troubleshooting](#13-troubleshooting)
14. [Roadmap and Future Iterations](#14-roadmap-and-future-iterations)
15. [Containerized Deployment (Docker Multi-Arch Images)](#15-containerized-deployment-docker-multi-arch-images)
16. [Excel Point-Table Import and Automated Parsing Tool](#16-excel-point-table-import-and-automated-parsing-tool)
17. [Glossary](#17-glossary)

---

## 1. Background and Core Features

Field-level devices in industrial environments (e.g., Siemens PLCs) typically use proprietary communication protocols (such as the S7 protocol), whereas upper-layer applications (SCADA, MES) favor international standard protocols (OPC UA). This project acts as a middleware hub for protocol translation, eliminating communication barriers between heterogeneous systems.

```text
  Siemens PLC         [ Protocol Gateway ]             Host System
 ┌─────────┐ S7 Proto ┌──────────────────┐  OPC UA  ┌───────────┐
 │ DB/M/I/Q│ ───────► │ Data Acquisition │ ───────► │ UaExpert  │
 │ Data Reg│          │ & Protocol Conv  │          │ SCADA/MES │
 └─────────┘          └──────────────────┘          └───────────┘
```

---

## 2. Architecture and Data Flow

The system employs an **"On-Demand Polling mechanism combined with a TTL-based cache invalidation strategy."** The system remains dormant (zero load) when there is no client access. Physical reads to the PLC are triggered *only* when an OPC UA client initiates a read request and the current cached data has expired.

```text
                         ┌──────────────────────────────────────────────┐
   Physical PLC          │                Gateway Process               │           Host System
 ┌──────────┐            │  ┌─────────┐            ┌──────────┐         │       ┌──────────┐
 │   PLC    │◄─S7 Read── ┤  │Tag Cache│◄── Read ───│  OPC UA  │         │◄OPCUA─│  Client  │
 │          │(Cache Exp) │  │(Val+TTL)│── Return ─►│  Server  │ Request │       │          │
 └──────────┘            │  └─────────┘            └──────────┘         │       └──────────┘
                         └──────────────────────────────────────────────┘
                         Callback-driven data acquisition avoids invalid background polling
```

**Request Handling and Routing Logic:**

1. **Cache Hit**: If the cache timestamp has not exceeded the pre-defined TTL (Time-To-Live), the memory-cached data is returned directly, **avoiding physical network I/O**.
2. **Cache Miss / Initial Read**: If the cache has expired or is uninitialized, the gateway issues an S7 read request via the Snap7 interface, updates the local cache, and returns the latest data.
3. **Silent State**: Without external client connections, the gateway maintains zero communication with the PLC, significantly reducing the load on field devices.

**Design Considerations:**

- **Device Protection**: High-frequency polling can exhaust PLC communication resources. The on-demand strategy coupled with TTL filters instantaneous high-frequency reads, providing debouncing and peak shaving.
- **Cache Validity (TTL)**: Users can balance data real-time performance and device load via the `cache_ttl_ms` configuration parameter.
- **Dynamic Callback Driven**: Leveraging open62541's DataSource mechanism, it enables lazy evaluation for node data, strictly distinguishing internal initialization from actual client requests.

---

## 3. Directory Structure

```text
S7_to_OpenUA/
├── Makefile                  # Build script (supports make, make test, make clean)
├── README_EN.md              # Project documentation (English)
├── config/
│   └── gateway.json          # ★ Core config: PLC connection info & tag list
├── include/                  # Interface declarations
│   ├── s7_types.h            #   ① S7 data types and byte decoding conventions
│   ├── config.h              #   ② System configuration data structures
│   ├── s7_client.h           #   ③ Physical communication layer (Snap7 wrapper)
│   ├── tag_cache.h           #   ④ Thread-safe TTL data cache module
│   └── opcua_server.h        #   ⑤ Protocol exposure layer (OPC UA server callbacks)
├── src/                      # Core source code
│   ├── s7_types.c
│   ├── config.c
│   ├── s7_client.c
│   ├── tag_cache.c
│   ├── opcua_server.c
│   └── main.c                #   Main program and lifecycle management
├── tests/                    # Unit and integration tests
│   ├── test_s7_types.c
│   ├── test_config.c         #   Config parsing and exception branches
│   ├── test_s7_client.c      #   S7 read/write tests using a mock server
│   ├── test_tag_cache.c      #   Concurrency, locks, and data consistency
│   └── test_opcua_server.c   #   End-to-end network communication tests
└── third_party/
    └── snap7/                # Snap7 pre-compiled binaries and headers
        ├── include/snap7.h
        └── lib/libsnap7.dylib
```

---

## 4. Core Modules Overview

The system strictly follows a bottom-up layered architecture, featuring high cohesion and low coupling, allowing independent modular testing.

### ① Data Decoding Layer (`s7_types`)

Responsible for converting Big-Endian raw byte streams from the network into standard C data structures.
- `s7_decode(type, buf, bit, &out)`: Polymorphic parsing function.
- Comprehensive support for standard S7 built-in types: `BOOL` / `BYTE` / `SINT` / `USINT` / `INT` / `UINT` / `WORD` / `DINT` / `UDINT` / `DWORD` / `REAL` / `LREAL`.

### ② Configuration Layer (`config`)

Uses cJSON to load and strongly type-map the `gateway.json` structure.
- Strict semantic validation: data type matching and boundary condition checks (e.g., DB limits, bit offset constraints), supporting a Fail-fast strategy.

### ③ Physical Communication Layer (`s7_client`)

Abstracts the complex Snap7 library interfaces into an object-oriented `S7Conn` handle model.
- Supports auto-reconnect mechanisms and exception state isolation.
- Wraps the `Cli_ReadArea` operation and links it with the data decoding layer for unified read and deserialization.

### ④ Data Cache Layer (`tag_cache`)

A high-performance, thread-safe data state container based on Pthreads.
- Composite data slot design: stores `Value + Quality Code + Timestamp`.
- Read-write separation: Utilizes Mutex locks for data snapshot isolation, ensuring atomic read/write transactions. Provides the core `get_fresh` interface to support on-demand polling decisions.

### ⑤ Protocol Exposure Layer (`opcua_server`)

Builds the OPC UA Address Space, mapping configured tags to DataSource variable nodes.
- Establishes a standard hierarchical structure: `Objects / <DeviceName> / <TagName>`.
- Automated quality code mapping: maps normal status to `Good` and offline status to `BadNoCommunication`, strictly adhering to OPC UA specifications.

### ⑥ Main Service Process (`main`)

Manages the gateway lifecycle:
1. Resource initialization (config loading, cache allocation).
2. Lazy connection establishment.
3. Starting the OPC UA main event loop.
4. Catching termination signals for Graceful Shutdown.

---

## 5. Key Technical Concepts

| Concept | Technical Implication | Module |
|---|---|---|
| **Big-Endian** | S7 protocol uses big-endian byte order; heterogeneous PC architectures (like x86/ARM little-endian) must perform endianness conversion. | `s7_types.c` |
| **Bit Masking** | Bit offset calculations for `BOOL` types within a byte sequence. | `s7_types.c` |
| **Memory Alignment & Boundary Protection** | Uses `memcpy` for floating-point and large integer deserialization to avoid bus errors caused by forced pointer casting. | `s7_types.c` |
| **Opaque Pointer** | Encapsulates C structures to hide internal implementation details and improve ABI stability. | `s7_client.h` |
| **DataSource Callbacks** | Lazy Evaluation mode, deferring data access until it is strictly required. | `opcua_server.c` |
| **TTL Cache Control** | Request coalescing and rate-limiting mechanism to shield physical layer bandwidth jitter. | `tag_cache.c` |
| **Exception Retention** | Standard industrial practice: upon communication interruption, retain old values, degrade data quality (e.g., `Bad`), and prohibit zeroing out. | `tag_cache.c` |

---

## 6. Environment and Dependencies

The recommended build and run environment is **macOS (Apple Silicon) + Apple clang**. Core dependencies include:

| Component | Function | Installation Guide |
|---|---|---|
| **clang / make** | Compiler suite and build tool | OS integration (Xcode Command Line Tools) |
| **cJSON** | Lightweight JSON parser | `brew install cjson` |
| **open62541** | Core OPC UA protocol stack | `brew install open62541` |
| **snap7** | S7 communication backend | Requires compilation from source (see below) |

### Appendix: Snap7 Compilation & Integration (First Build Only)

Since Homebrew does not currently provide pre-compiled packages for macOS, this project includes adapted compilation artifacts. To rebuild the backend library environment, follow these steps:

```bash
# 1. Obtain the core Snap7 source package
curl -L -o /tmp/snap7.7z "https://master.dl.sourceforge.net/project/snap7/1.4.2/snap7-full-1.4.2.7z?viasf=1"
brew install sevenzip && 7zz x -y /tmp/snap7.7z -o/tmp

# 2. Cross-compile to build the dynamic library (removing Linux-specific -lrt flags)
cd /tmp/snap7-full-1.4.2/src
clang++ -O3 -fPIC -shared -Isys -Icore -Ilib \
  sys/*.cpp core/*.cpp lib/*.cpp -lpthread -o libsnap7.dylib

# 3. Integrate into the project structure
cp libsnap7.dylib  <ProjectRoot>/third_party/snap7/lib/
cp ../release/Wrappers/c-cpp/snap7.h  <ProjectRoot>/third_party/snap7/include/
install_name_tool -id @rpath/libsnap7.dylib  <ProjectRoot>/third_party/snap7/lib/libsnap7.dylib
```

---

## 7. Build and Compile

The project utilizes a standard Makefile for automated build management.

```bash
make            # Standard build, generates ./gateway executable program
make test       # Builds and automatically runs all unit and integration tests
make clean      # Cleans target files and execution artifacts
make tsan       # (Advanced) Data race analysis via ThreadSanitizer
```

---

## 8. Configuration Parameters

The gateway's runtime behavior is centrally managed via `config/gateway.json`. Developers must configure the device connection endpoints and data tag lists corresponding to their actual network topology.

```jsonc
{
  "opcua": { "port": 4840 },               // [Optional] OPC UA listen port (default 4840)
  "collection": { "cache_ttl_ms": 1000 },  // [Optional] Cache TTL in ms (default 1000)
  "plcs": [                                // Physical PLC network list
    {
      "name": "Line1_PLC",                 // [Required] PLC Identifier (Forms NodeId Namespace)
      "ip": "192.168.0.1",                 // [Required] Target IPv4 address
      "port": 102,                         // [Optional] ISO-on-TCP port (default 102)
      "rack": 0,                           // [Optional] Hardware rack number (default 0)
      "slot": 1,                           // [Optional] CPU slot number (default 1)
      "tags": [                            // Tag data acquisition list
        { "name": "Motor1.Speed", "area": "DB", "db": 10, "start": 0,  "type": "REAL" },
        { "name": "Motor1.Run",   "area": "DB", "db": 10, "start": 4,  "bit": 0, "type": "BOOL" },
        { "name": "Motor1.Count", "area": "DB", "db": 10, "start": 6,  "type": "DINT" }
      ]
    }
  ]
}
```

### Tag Description Specification

| Property | Semantic Definition | Valid Range | Required |
|---|---|---|---|
| `name` | Tag Identifier | Valid alphanumeric string | ✅ |
| `area` | Memory Area | `DB` (Data Block), `M` (Memory), `I` (Inputs), `Q` (Outputs) | ✅ |
| `db` | DB Module Number | Positive Integer (only for `DB` area) | Required if `DB` |
| `start` | Byte Address Offset | Non-negative integer | ✅ |
| `bit` | Bit Address Offset | `0` ~ `7` (only for `BOOL` type) | Required if `BOOL` |
| `type` | Scalar Data Type | See Type Mapping Conventions Table below | ✅ |
| `node_id` | Explicit OPC UA NodeId (overrides auto-generation settings) | Standard NodeId format, e.g. `ns=2;s=[1001001]` | Optional (see Chapter 16) |

> **Configuration Note**: `node_id` is an optional field aimed at providing backward compatibility and flexible integration capabilities. If specified, the gateway aligns with the assigned NodeId to expose nodes, facilitating seamless mapping with field engineering point tables or external SCADA systems. If omitted, the system defaults to generating NodeIds using the `ns=1;s=PLCName.TagName` standard structure.

### Type Mapping Conventions

| Config Type | S7 Equivalent | Size | OPC UA Model Mapping | Example Address |
|---|---|---|---|---|
| `BOOL`  | Boolean        | 1 Bit   | `Boolean` | `DB10.DBX4.0` |
| `BYTE`  | Unsigned 8-bit | 1 Byte  | `Byte` (UInt8) | `DB10.DBB0` |
| `USINT` | Unsigned 8-bit | 1 Byte  | `Byte` (UInt8) | `DB10.DBB0` |
| `SINT`  | Signed 8-bit   | 1 Byte  | `SByte` (Int8) | `DB10.DBB0` |
| `WORD`  | Unsigned 16-bit| 2 Bytes | `UInt16` | `DB10.DBW0` |
| `UINT`  | Unsigned 16-bit| 2 Bytes | `UInt16` | `DB10.DBW0` |
| `INT`   | Signed 16-bit  | 2 Bytes | `Int16`  | `DB10.DBW0` |
| `DWORD` | Unsigned 32-bit| 4 Bytes | `UInt32` | `DB10.DBD0` |
| `UDINT` | Unsigned 32-bit| 4 Bytes | `UInt32` | `DB10.DBD0` |
| `DINT`  | Signed 32-bit  | 4 Bytes | `Int32`  | `DB10.DBD0` |
| `REAL`  | IEEE 754 Float | 4 Bytes | `Float`  | `DB10.DBD0` |
| `LREAL` | IEEE 754 Double| 8 Bytes | `Double` | `DB10.DBD0` |

---

## 9. Running Guide

Start the gateway process:

```bash
./gateway                       # Loads default config: config/gateway.json
./gateway /path/to/custom.json  # Loads specified custom configuration file
```

Standard runtime output:

```text
[System] Configuration loaded: config/gateway.json (PLCs: 1, Port: 4840, Cache TTL: 1000ms)
[System] Gateway initialized (On-demand polling). Listening at: opc.tcp://localhost:4840 (Exit signal: SIGINT/Ctrl+C)
[System] Note: PLC will not be accessed without client requests.
```

> **Runtime Mechanism Note**: Based on the **on-demand polling** and **lazy connection** mechanisms adopted by the system, the gateway **will not** actively establish a communication link with the PLC upon startup initialization. The underlying network connection and data fetching operations are triggered only when an OPC UA read request is received and the corresponding node's local cache is identified as expired. Therefore, early startup logs omit connection state info; connection confirmations (e.g., `[FakeLine] Connected ...`) will exclusively be recorded once an actual data interaction materializes.

The system comprehensively supports Graceful Shutdown, catching termination signals to ensure complete execution of resource recycling and cleaning routines.

### Virtual Simulation Mode

This project incorporates the `fake_plc` simulation tool component to furnish reproducible test signals and simulate dynamic process data, effectively alleviating dependencies on physical hardware environments during development and integration stages.

1. **Initialize Simulation Environment** (Launch the mock PLC process, bounded to local port 1102):
   `make fake_plc && ./fake_plc`
2. **Start Gateway Service** (Load the configuration optimized for the simulation environment):
   `make gateway && ./gateway config/fake_plc.json`
3. **Run Client Monitoring Tool** (Periodically monitor node data states):
   `make ua_monitor && ./ua_monitor config/fake_plc.json [Refresh Interval in Seconds]`

   The `ua_monitor` command accepts an optional **refresh interval parameter** in seconds. It supports floating-point assignments with a minimum bound of 0.1s. Default behavior executes at 1s intervals:

   ```bash
   ./ua_monitor config/fake_plc.json 0.5   # Configure to issue a read request every 0.5s
   ./ua_monitor config/fake_plc.json 5     # Configure to issue a read request every 5s
   ./ua_monitor config/fake_plc.json       # Utilize default logic, issuing a request every 1s
   ```

> **System Frequency Concept Resolution**: Throughout the operation cycle, the system is governed by two mutually independent frequency mechanisms:
> - **Client Polling Frequency**: Regulated by the `ua_monitor` argument, dictating how frequently external clients issue data reading solicitations to the gateway.
> - **Physical Acquisition Limitation Frequency (TTL)**: Established by the `collection.cache_ttl_ms` in the configuration file, enforcing the maximum authorized frequency for gateway-initiated physical network I/O reads toward the PLC.
>
> **Underlying Load Assessment**: The ultimate frequency of physical PLC reading operations is governed mutually by the above directives, with the **lower frequency** acting as the decisive limit. For example: if the client probes at 0.5s intervals, yet the gateway TTL threshold dictates 3s, physical S7 reading sequences are triggered exclusively every 3s. Excess queries within that window are promptly resolved by the gateway's cached snapshots. **Optimally adjusting the TTL parameter serves as the fundamental control strategy for ensuring physical PLC stability and avoiding bus overloads.**

---

## 10. System Verification and Client Connection

When executing system integration and interoperability verifications, it is highly recommended to leverage standard third-party OPC UA client applications (e.g., [UaExpert](https://www.unified-automation.com/products/development-tools/uaexpert.html)):

1. Ensure the gateway server process is active (`./gateway`).
2. Within the client software, configure the Endpoint URI (e.g., `opc.tcp://127.0.0.1:4840`) and establish a connection.
3. Select an "Anonymous" credential strategy to orchestrate the application session.
4. Browse the server's Address Space: Unfold the hierarchical path `Objects → Line1_PLC`, navigate to the target data node, and strictly monitor the real-time consistency of the node's numerical Value alongside its associated communication Quality code.

---

## 11. Unit and Integration Testing

The system embeds a multi-dimensional suite of automated test cases to yield high-coverage verification across principal business paths:

- **Data Type Decoding Verification**: Performs boundary checks on precision logic translating network-layer big-endian byte sequences to internal host memory types.
- **Protocol Stack Simulation Checks**: Utilizes the Snap7 Server component to architect virtual data response modules, contrasting reading accuracies by injecting pre-defined memory blocks.
- **Concurrency and Synchronization Analysis**: In high-frequency, multi-threaded read/write scenarios, conducts consistency assertions targeting the monotonic integrity and isolation effectiveness of cache state machines.
- **End-to-End Integration Assessments**: Simulates genuine request pipelines, performing assertion tests mapped to request and response status transitions throughout the complete service flow.

Engineers may orchestrate the entire automated test suite by invoking the `make test` pipeline command.

---

## 12. Hardware Integration Guide (Important)

For deployment involving physical S7-1200 / S7-1500 controllers inside production operations, the following engineering protocols must be strictly configured via TIA Portal (Totally Integrated Automation Portal):

1. **Enable PUT/GET Communication Privileges**:
   Navigate to the Device View and enter the CPU Properties panel. Proceed to "Protection & Security -> Connection mechanisms", and strictly ensure that the checkbox for "Permit access with PUT/GET communication from remote partner (PLC, HMI, OPC, etc.)" is enabled.
2. **Disable Optimized Block Access Property for Target Data Blocks**:
   The current gateway implementation relies upon traditional absolute addressing mechanisms for data acquisition. Consequently, any DB block housing exposure-target process variables must have its property panel modified to uncheck the "Optimized block access" configuration.
3. **Architectural Addressing Parameter Specification**:
   Within the gateway's `gateway.json` file, the parameters for `rack` (rack number) and `slot` (slot number) must precisely match the device's hardware topology. Baseline configuration values encompass:
   - For S7-1200 / S7-1500 system architecture, the conventional alignment requires: `rack = 0, slot = 1`.
   - For S7-300 / S7-400 system architecture, the conventional alignment requires: `rack = 0, slot = 2`.

---

## 13. Troubleshooting

When encountering runtime anomalies during deployment and debugging, operators may consult the following diagnostic strategies:

| Symptom Display | Diagnostic Pathway and Remediation Strategy |
|---|---|
| `Connection refused` | Implies a failure in establishing the foundational TCP link. Verify target IP reachability, PLC power state, and critically ensure TCP Port 102 traverses all network firewalls seamlessly. |
| Value Drift or Data Decoding Faults | Frequently induced by discrepancies in address mappings or type width incongruities. Re-confirm that the target DB within the TIA Portal strictly disables "Optimized block access"; symmetrically, perform an audit on byte and bit offset geometries defined in the JSON configuration. |
| Access Rejected / Protocol-Level Communication Denials | Suggests normal network layering but execution refusal by the S7 protocol stack. An overriding cause revolves around PLC hardware configs lacking PUT/GET privileges. Proceed to activate this in the engineering workstation software. |
| Boolean Type (BOOL) Persistently Registering `false` | Acknowledged as a bitwise mask extraction logic failure. Conduct an inspection of the JSON config parameters focusing on `start` (byte offset) and `bit` (bit offset, mandatory range 0-7) validities. |
| Process Initialization Error: OPC UA Port Conflict | Indicates the configured listen port (default 4840) has been preempted by extraneous network services. Leverage OS tools (e.g., `lsof -i :4840`) to isolate the offending process, or reconfigure the `opcua.port` attribute parameter within the configurations to redirect traffic. |

---

## 14. Roadmap and Future Iterations

**Currently Delivered Core Functional Modules**: Heterogeneous multi-device node integration handling, structured JSON-centric data mapping schemas, TTL-controlled and on-demand polling-driven data routing dissemination, strictly read-only protocol layer transformations, robust communication linkage state trapping accompanied by degradation preprocessing, **integrated independent Excel point-table import and Siemens address auto-parsing tooling**, and **support for customizable NodeID-guided node exposure deployments**.

**Anticipated Technological Evolution Trajectory**:
- **Bidirectional Data Links**: Introduction of data write capabilities allowing downstream control directive assignments.
- **Underlying Physical Transport Optimizations**: Aggregation of Protocol Data Unit (PDU) concurrency management and contiguous memory block sequential reads to surmount physical transport bus bottlenecks and maximize throughput.
- **Service Security Infrastructure Fortification**: Adoption of X.509 certificate identity authentication protocols combined with Transport Layer Security encryption to fulfill industrial internet security compliance mandates.
- **Complex Hierarchical Data Schema Mapping**: Expansion into object-oriented data modeling frameworks supporting arbitrary-length strings (STRING) and multi-dimensional array mapping translations.
- **Dynamic Configuration Hot-Reload Capabilites**: Enabling dynamic loading and seamless application of modified tag configuration sets while bypassing interruptions to the core service process execution.

---

## 15. Containerized Deployment (Docker Multi-Arch Images)

A prebuilt **multi-architecture Docker image** (`linux/amd64` + `linux/arm64`) is published, covering x86 servers, ARM edge devices (e.g. Raspberry Pi), and Apple Silicon macOS. The image is ready to run — no compilers or dependencies need to be installed on the target machine.

Image: [`impxssive/s7-opcua`](https://hub.docker.com/r/impxssive/s7-opcua) (Docker Hub).

Pull the image:

```bash
docker pull impxssive/s7-opcua:0.1.1
```

The image bundles the **Excel point-table converter** (Python 3 runtime + `openpyxl` + `tools/xlsx_to_config.py`); the entrypoint supports two startup modes:

```bash
# Mode 1: load a JSON config directly (standard)
# mount your host config directory to /app/config via -v
docker run --rm -p 4840:4840 -v /host/config-dir:/app/config \
  impxssive/s7-opcua:0.1.1 config/your-config.json

# Mode 2: feed an Excel point-table; it is converted inside the container, then the gateway starts
docker run --rm -p 4840:4840 -v /host/config-dir:/app/config \
  impxssive/s7-opcua:0.1.1 --xlsx /app/config/points.xlsx \
  --ip 192.168.0.10 --plc-name FurnacePLC
```

> **Notes**:
> - After `--xlsx` comes the Excel file path; remaining args (`--ip`, `--port`, `--plc-name`, `--opcua-port`, `--cache-ttl-ms`, …) are forwarded to the converter (see Section 16). Chinese tables must include `--ip`.
> - Ports: `-p 4840` exposes the OPC UA server; the gateway dials out to the PLC on port 102, so Docker's default bridge network suffices.
> - In production, prefer a pinned version (e.g. `0.1.1`) over `latest` for reproducibility.

---

## 16. Excel Point-Table Import and Automated Parsing Tool

Within the deployment execution phase of industrial control integrations, field sensor mapping relations are predominantly administered and disseminated utilizing Excel electronic spreadsheet formats, whereas the gateway's underlying architectural engine rigorously enforces a standardized JSON data structuration as its sole configuration conduit. Addressing this implementation discontinuity, the project ecosystem introduces an **independently decoupled Python-based automated conversion tool script** (`tools/xlsx_to_config.py`). This utility is dedicated exclusively to achieving a lossless transposition from Excel point-table datasets into the structured JSON configuration semantics necessitated by the gateway:

```text
Field Excel Point-Table Source File(.xlsx)  ──►  xlsx_to_config.py Conversion Script  ──►  Structured JSON Config File  ──►  Gateway Core Process Injection
```

> **Tooling Boundary Disclaimer**: This script acts as an auxiliary and autonomous tool suite. The gateway's central C-based service routine will eschew invoking this program at any operational interval, establishing no runtime dependencies upon its presence. Prior to script initiation, operators must verify the host environment contains a fully provisioned `openpyxl` dependency module. For operators utilizing macOS operating systems constrained by PEP 668 environment regulatory policies, executing the script via the project's native Python virtual environment workflow is advised:
> Step 1: Environment construction: `python3 -m venv .venv && ./.venv/bin/python3 -m pip install openpyxl`
> Step 2: Script execution: `./.venv/bin/python3 tools/xlsx_to_config.py <parameter_arguments>`

Possessing an adaptive formatting recognition engine, the converter tool seamlessly classifies **two** archetypical data table structural templates, primarily hinging upon intelligent header attribute match resolutions (conversely, users may forcefully impose a template declaration passing the `--format cn|simple` command line parameter).

### 16.1 Industrial Standard Format Template (cn format standard)

This template deeply resonates with established domestic industrial deployment paradigms: incorporating standardized Chinese header categorizations, utilizing the Siemens-endorsed absolute addressing representation, and accommodating customizable NodeID configuration capacities. The specific mapping relations bridging the data columns to their corresponding objects are articulated below:

| Excel Column Nomenclature Standard | JSON Configuration File Mapped Property | Parsing and Processing Strategy Discourse |
|---|---|---|
| `点位名称` (Point Title) | Mapped to the `name` attribute belonging to the tag array element (serving as the unique point identifier). | This functions as a compulsory required field. In instances of field value omission, the parsing logic automatically regresses to harvest the `点位ID` (Point ID) field parameter to serve as a surrogate naming fill-in. |
| `点位地址` (Point Address) | Aggregatively parsed into a comprehensive subset: `area`, `db`, `start`, and `bit` property assignments. | The core engine envelops a smart parsing module grounded in the Siemens absolute addressing methodology. Specific algorithmic rules and boundary confines are elucidated in Section 16.1.1. |
| `数据类型` (Data Type) | Mapped to the `type` attribute belonging to the tag array element. | Values will be transcribed into the generic data type definitions standardized at the gateway's baseline, concurrently dictating the read span dimension corresponding to the underlying memory sectors. Please consult Section 16.1.2. |
| `NodeID` | Mapped to the `node_id` attribute belonging to the tag array element. | The parsing utility conducts a lossless passthrough execution transmitting this entity to the output script. Succeeding protocols instruct the gateway mechanism to orchestrate the deployment and exposure of the targeted OPC UA node conforming to this bespoke assignment (Detailed in Section 16.4). |
| `点位ID` (Point ID) | Auxiliary sensor point identification redundant repository. | Exists in a dormant modality, activated exclusively when the premier `点位名称` property field evaluates as devoid of input, whereupon it serves as an additive identifier alternative. |
| `单位` / `最新数据` / `更新时间` / `设备号` (Unit / Latest Data / Update Timestamp / Equipment Tag) | Systematized proactive circumvention and rejection. | The operational logic categorizes these informational facets as belonging to the runtime state properties architecture, consequently eradicating them from the systematic compilation processing chain yielding the static configuration file. |

**Underlying Network Connection Information Injection Mechanism**: As this specific point table layout lacks the capability to encapsulate network addressing topologies for downstream controllers (e.g., explicit IP routing information), it is obligatory to introduce corresponding linkage settings via external command-line parameters injected during the transformation tool's operational phase (Design mandate: A single autonomous configuration file delineates the communication topography binding to a sole physical PLC unit):

```bash
./.venv/bin/python3 tools/xlsx_to_config.py TargetPoints.xlsx -o config/output.json \
    --ip 192.168.0.10 --port 102 --rack 0 --slot 1 --plc-name TargetPLC \
    --opcua-port 4840 --cache-ttl-ms 1000
```

> **Command Line Argument Orchestration Outline**: Utilizing the `cn` parsing mode strictly mandates `--ip` provisioning. Concurrently, the system assumes default assignments for extraneous network parameters (i.e., `--port` registers at 102, `--rack` allocates 0, `--slot` allocates 1, and the `--plc-name` identification marker defaults to PLC1).

#### 16.1.1 Addressing Syntax Deciphering Regulations → Transmutation into area / start / bit Cluster Attributes (Aligned with Siemens Absolute Addressing Methodology)

| Syntactical Address Sample Construct | Resultant System Analytical Yield Cluster | Underlying Memory Spatial Address Routing Mapping |
|---|---|---|
| `DB1.DBX8.0` | Extracts `area=DB`, `db=1`, `start=8`, simultaneously capturing **`bit=0`** | Resolves pointer aiming towards a Boolean discrete variable entity (BOOL) residing inside Data Block DB. |
| `DB1.DBB2` | Extracts `area=DB`, `db=1`, `start=2` | Resolves pointer aiming towards a singular byte entity residing within Data Block DB, commencing at offset 2. |
| `DB1.DBW2` | Extracts `area=DB`, `db=1`, `start=2` | Resolves pointer aiming towards a Word-categorized variable entity (occupying a 2-byte volume) residing within Data Block DB, commencing at offset 2. |
| `DB1.DBD0` | Extracts `area=DB`, `db=1`, `start=0` | Resolves pointer aiming towards a Double Word-categorized variable entity (occupying a 4-byte volume) residing within Data Block DB, commencing at offset 0. |
| `M10.0` / `MB10` / `MW10` / `MD10` | Comprehensively extracts `area=M`, anchored on baseline offset `start=10` (appending `bit` value if structured as bitwise notation) | Projects accessibility mapping towards internal Memory Marker flag registries. |
| `I0.0` / `IB0` / `IW0` / `ID0` | Uniformly extracted and indexed as `area=I` | Projects accessibility mapping towards hardware-linked external physical Input process imagery registries. |
| `Q0.0` / `QB0` / `QW0` / `QD0` | Uniformly extracted and indexed as `area=Q` | Projects accessibility mapping towards hardware-linked external physical Output process imagery registries. |

> **Parsing Logic Mechanisms and Fault-Tolerance Strategy Declaration**: The parsing translation engine is authorized to harvest the **initiatory byte memory alignment index** utilizing the alphabetic syntax encodings presented in the address syntax (explicit identifiers corresponding to `X/B/W/D`); notwithstanding, **the fundamental determinant governing the comprehensive memory sector volume triggered during subsequent downstream operations invariably defers to the parameters stipulated under the column cataloged as "Data Type"**. Consequentially, should incongruences emerge wherein data type dimensional declarations contravene the width definitions mapped by the syntax encoding address descriptions (e.g., explicitly defining a data tier categorized as `float32` necessitating 4-bytes volume while furnishing an address cell string manifesting `DB1.DBW2` denoting 2-bytes), the conversion utility executes a non-blocking mitigation response: delivering an alert notification distributed strictly through standard outputs warning the operator whilst simultaneously preserving continuous pipeline momentum, forcing downstream configuration instructions aligned unconditionally with the byte width constrained by the defined data type classification (asserting a 4-byte read instruction framework for the prior example). In pursuit of averting latent incalculable deployment hazards, it is vehemently recommended to integrate more robust and idiomatic address alignment documentation styles (e.g., standardizing the entry as `DB1.DBD2` for the hypothetical referenced).

#### 16.1.2 Predefined Data Type Mappings Roster (Source Data Column Contents → Associated Transformations Mapping Gateway Regulated Types)

The transformation application logic mechanism introduces a comprehensive case-insensitive processing capability to automatically govern matching alignments across an extensive inventory covering prevalent engineering declaration syntaxes, identifying and integrating the subsequent:

| Excel Cell Data Entry Classification Syntax | Internal Mapped Implementation Gateway Reference Topology | Theoretical Memory Architecture Allocation Metric Constraint | OPC UA Protocol Mapping Transposition Topology Equivalency |
|---|---|---|---|
| `bool` / `bit` | BOOL | Claims 1 Discrete Bit Allocation | Associates to `Boolean` Type Ecosystem |
| `int8` / `sint` | SINT | Claims 1 Byte Allocation | Associates to `SByte` Type Ecosystem |
| `byte` / `uint8` / `usint` | BYTE | Claims 1 Byte Allocation | Associates to `Byte` Type Ecosystem |
| `int16` / `int` | INT | Claims 2 Bytes Allocation | Associates to `Int16` Type Ecosystem |
| `word` / `uint16` / `uint` | WORD | Claims 2 Bytes Allocation | Associates to `UInt16` Type Ecosystem |
| `int32` / `dint` | DINT | Claims 4 Bytes Allocation | Associates to `Int32` Type Ecosystem |
| `dword` / `uint32` / `udint` | DWORD | Claims 4 Bytes Allocation | Associates to `UInt32` Type Ecosystem |
| `float32` / `real` / `float` | REAL | Claims 4 Bytes Allocation | Associates to `Float` Type Ecosystem |
| `float64` / `lreal` / `double` | LREAL | Claims 8 Bytes Allocation | Associates to `Double` Type Ecosystem |

> **Contingency Strategy Responding to Border Constraints Violations**: Immediately upon system tracking algorithms intercepting array inputs within data arrays lacking explicit presence mapped inside the referenced directory syntax logs above, the conversion logic provokes an absolute execution blockade and relays the unmodified anomaly sequence input directly onto the command line interface framework serving to compel and direct personnel intervention rectifying the data table origination documentation.

### 16.2 Explicit Attribute Mapping Point-Table Format Layout (simple format standard)

Predicated on the scenario where an enterprise orchestrates variable attribute schemas by decoupling configuration datasets into discrete parameters arrayed across independently allocated columns—consequently negating the obligation to process analytical syntactic breakdown algorithms focusing on address decoding—the utility uniformly implements dedicated operational support embracing the `simple` template structure. Prescribed to this formatting environment, architectural parameters enforce strict adherence commanding the introductory ledger structure (the table header hierarchy) to orchestrate data sets according to the precisely formalized succession list detailed as:
`plc_name ip port rack slot poll_interval_ms tag_name area db start bit type`

Embedded securely into this formatting design, internal table metrics interlink comprehensively, projecting an identical unmediated direct mapping correspondence parallel with internal attribute mappings located inside the core JSON implementation architecture mapping structures. Correspondingly, numeric values controlling hardware networking protocol bindings seamlessly decouple into the system utilizing straightforward extraction operations sourced directly from assigned independent column mappings (thusly obviating necessity calling for commanding parameter execution via `--ip`), furthermore extending compatibility granting centralized encapsulation integrating configuration assets bridging distinct PLC hardware controllers merged safely into unified standalone sheet assets (relying purely on a systematic `plc_name` query methodology enforcing automatic clustering distributions).

### 16.3 System Diagnostic Overviews Involving Data Check Validations and Status Escaping Protocols

Strategically directed toward radically enhancing diagnostic efficiency indices mapped alongside systems implementation routines bridging deployment stages, the companion structure utility embraces robust data sanity validation processing cycles enforcing global verifications ensuring uninterrupted endpoint connectivity chains. Throughout active transition compilation operations, script processes execute exhaustive sweeping analysis spanning completely all primary data stream ingestions ensuring total validation completion checks prior to emitting finalized fault consolidations collected securely via terminal output interface displays, proactively eliminating repetitive cycle interruptions indicative of rudimentary piecemeal correction mechanisms.

Operational parameters mapped against verification operations are architected fundamentally drawing from foundational `config.c` validation checks entrenched into the primary core service module algorithms traversing multiple validation arenas integrating yet notably expanding beyond constraints regarding: semantic string confirmation mapping towards data repository classification validations, verifying implicit data operations linked inside proprietary `DB` zone manipulations possess requisite legitimate indexing metrics dependencies validations, guaranteeing logical compliance surrounding discrete `BOOL` retrieval functionalities asserting integral dependencies associated alongside valid `bit` property inputs conforming securely inside `0~7` span limitations bounds, maintaining administrative controls blocking excessive data length array boundary violations traversing key equipment title nomenclatures combined with IPv4 string metrics definitions protocols, executing cross-checking data validation algorithms certifying all underlying metrics distributed beneath grouped host hardware device architectures uniformly observe cohesive internal logic integrations protocols, whilst continuously orchestrating conflict prevention logic affirming total eradication of repetitive tag nomenclature implementations targeting individual controlled cluster environments.

Protocol guidelines asserting diagnostic feedback implementations mapping state responses yield as follows:
- Responding System Return Value `0`: Positively indicating comprehensive formatting processing execution logic completed unfettered arriving gracefully towards finalized compilation completion states.
- Responding System Return Value `1`: Diagnostic parameters isolate point documentation variables manifesting critical discrepancies breaching **content semantic definitions properties thresholds** indicating logical conflicts mapping standard rules deviations (command interface terminals shall yield iterative localized alert tracing catalogues directing personnel manual data rectification remediation processing sequences preceding any auxiliary secondary generation loads instructions requests).
- Responding System Return Value `2`: Dispatching critical failure alerts denoting platform logic executions trapped into extreme terminal interruption system incidents severely stalling ongoing pipeline progressions mapping severe anomaly generation contexts (cataloged triggers encompassing variables incorporating critically fractured matrix architecture property lists, access obstructions terminating data reading interactions attributed directly toward strict authorization isolation protocols exerted by parent OS structures over primary repository file pathways, environmental prerequisites deficiencies indicating foundational module resources missing like unlocated `openpyxl` utility arrays, unresolvable localization failures rooted inside invalid custom sheet identification references preventing primary anchoring routines or fundamentally unparseable algorithmic injections introduced via erroneous execution instructions distributions errors and other fundamentally impenetrable external factors mapping insurmountable failures).

### 16.4 Node Exposure Operations Directed Via Custom NodeID Standard Constraints Architecture

Sustained via advancements introduced throughout the progression cycles encompassing next-generation structural configurations documents integrating independent deployment support mechanisms linked inside the `node_id` parameters (e.g. implementing standard typologies mapping strings such as `ns=2;s=[1001001]`), expansive functional implementations aggressively bolster system capacity delivering directed orchestration resolving target node mapping exposition schemas injected onto universally generated OPC UA spatial topology addressing layers. Fundamental characteristic upgrades map robust support protocols responding positively aligning compliance constraints asserted towards downstream complex supervisory systems (comprising operational frameworks matching SCADA central processing nodes) obligating rigorous specific NodeID constraint metrics implementation definitions frameworks. Within sprawling heterogeneous network topography industrial deployments, the characteristics perfectly construct a **frictionless zero-barrier transition mapping capacity facilitating unperceived hot-swappable equipment migration upgrades mechanisms integrations mapping systemic continuity parameters optimizations**.

Configurations logic implementation protocol action execution sequences paradigms:
- Given scenarios locating active parsing processing sequences explicitly determining particular child entity components asserting presence **explicitly hosting documented payload assignments** mapped over internal `node_id` parameters, underlying core gateway protection subroutines unequivocally adopt enforced priority directives extracting explicit custom NodeID variable tokens mapping targeted Node execution building configurations orchestrating targeted network broadcasts publishing routines processes (inclusive automatic internal routines generating namespace unregistration mapping unarchived Namespace environments comprising representations echoing parameters matching properties aligning closely with structures like `ns=2`).
- Reciprocally, confirming target point parameter definitions documents indicating environments **utterly void of detection capabilities** regarding data value deployments focusing explicitly upon targeted `node_id` entries, intrinsic structural gateway address location algorithms unilaterally switch modes embracing rollback operational patterns focusing heavily relying upon auto-generating composite data compilation algorithms dynamically piecing comprehensive placeholders filling address voids structured typically following templates projecting structures like `ns=1;s=TargetedHostPLCAppellation.SpecificEntityPointAppellation` (imposing strategic controls asserting uncompromising compliance guaranteeing previous data configuration documentation formats flawlessly shift forward carrying extensive **complete backward integration interoperability features inheritance mechanisms capabilities** post engine upgrades sequence terminations).

It must be explicitly iterated confirming: introduction parameters integrating this precise capability functionality maps solidly categorizing operations toward protocol mapping topology sublayers representing distinctly **wholly optional entirely supplementary logical characteristics optimization additions routines implementation enhancements**; core system execution mechanisms navigating operational environments processing legacy configuration formats entirely devoid indicating entries corresponding explicitly against mapped `node_id` metrics manifest processing routines identically without deviating operational variances mapped against traditional legacy execution pipeline results logic algorithms processing behaviors. In symmetry, associated linked native diagnostic monitoring terminal tools (`ua_monitor`) similarly fully align upgrade metrics introducing bespoke decoding characteristics features deeply linking mapping NodeID standardized configurations mechanisms executing full parameter synchronization capabilities securing operational continuity across lowest conversion tiers mapping entirely towards uppermost global diagnostic panel views maintaining strictly unyielding measurement equivalence continuity parameters effects.

### 16.5 Entire System Integration Workflow Conversions Configurations Validations Exercises Practical Full Scenarios Demonstrations Protocols

For the purpose of reducing the learning curve for development engineering personnel engaging complex integration operational deployments and catalyzing engineering environment development accelerations, the repository includes a practical demonstration data generator. The fundamental configuration objective mapping this specialized generator asserts independence forging synthetically replicated comprehensive data arrays structurally emulating accurate Chinese styled configuration point tables perfectly bridging matching physical memory mapping layouts strictly associating alongside memory sector Block 1 (DB1) variables generated inherently utilizing corresponding localized mock testing component assets deployed via the parallel software automation systems engine (`fake_plc`). (Outputs generated functionally serve advancing seamless verifications tests mapped extensively navigating closed-loop pipeline integrations bridging endpoints validations interactions). Operational workflow execution guidelines articulate operations logic implementations sequences protocols mirroring actions mapping:

```bash
# Workflow Operating Instructions Directives: Step 1: Engage trigger operations launching sample generation processes building simulated environment structural demonstrations assets mapping industrial communications point spreadsheets resources configurations examples/cn_points.xlsx
./.venv/bin/python3 examples/make_cn_points_xlsx.py

# Workflow Operating Instructions Directives: Step 2: Implement operations resolving formatting configurations systems structural mappings operations translations transformations manipulating extracted primary resources files whilst concurrently inserting critical operational routing protocols assigning connections networking properties mappings pointing precisely across loopback hosts anchoring localized idle instances mimicking soft programmable component states (fake_plc) structures mechanisms units.
./.venv/bin/python3 tools/xlsx_to_config.py examples/cn_points.xlsx \
    -o config/cn_fake.json --ip 127.0.0.1 --port 1102 --plc-name TargetPLC --cache-ttl-ms 3000
```

Successfully processing systems yield compiled outputs distributing JSON standardized structural settings files mapping essential subcomponents logic construction parameters configurations snippets showcasing fully developed example outcomes visual representations implementations arrays (direct particular inspection towards values asserting customized implementation matrices corresponding closely assigning inputs directly into `node_id` properties locations supplemented uniquely matching localization nomenclature mapping symbolic identifiers formats):

```json
{
  "opcua": { "port": 4840 },
  "collection": { "cache_ttl_ms": 3000 },
  "plcs": [
    { "name": "TargetPLC", "ip": "127.0.0.1", "port": 1102, "rack": 0, "slot": 1,
      "tags": [
        { "name": "FurnaceTopSouthPressureMonitoring", "area": "DB", "db": 1, "start": 0, "type": "REAL",
          "node_id": "ns=2;s=[1001001]" },
        { "name": "MainDriveMotorLiveRunStateMonitoring", "area": "DB", "db": 1, "start": 8, "bit": 0, "type": "BOOL",
          "node_id": "ns=2;s=[1001003]" }
      ]
    }
  ]
}
```

Advancing sequentially past primary stage implementation checkpoints, remaining operations strictly dictate execution procedures executing protocol rules mirroring deployment instructions detailed inside manual chapter 9 orchestrating concurrent execution loops triggering tripartite integrated operational verifications tests validations bridging underlying support environments clusters (enacting staggered operational starts accommodating: engaging hardware mock generation source signals structures mapping `fake_plc` host elements, subsequent processing loading strictly regulated recent output generation custom configurations mappings `gateway config/cn_fake.json` protocols implementing principal bridging routing mappings layers converting operational pipelines running daemon class core structures, ultimately capping off pipeline deployment executing operations visualization tools implementing terminal interface diagnostic captures metrics processing `ua_monitor config/cn_fake.json` data aggregations inputs pipelines tools mechanisms), verifying successful operations outcomes immediately subsequently permits terminal observation panels monitoring processes successfully securing data retrieval events extracting realtime dynamic measurement inputs mapped against deployed environments accurately utilizing NodeID strings formats identical configurations inputs specifications replicating precise syntaxes mapping `ns=2;s=[1001001]`, fully concluding validations mechanisms securing entire architectural framework ecosystem integrations closing active connectivity operations validations operational successes definitions mapping endpoints.

---

## 17. Glossary

| Core Terminology Nouns Vocabulary | Professional Definitions Categorizations Scopes Frameworks Boundary Expositions Interpretations Validations |
|---|---|
| **S7 Protocol Communications Suite** | Defines a closed-standard proprietary network communications semantic protocol suite framework structurally encapsulated predominantly guided by Siemens enterprise structures anchored fundamentally atop underlying ISO-on-TCP architecture definitions frameworks methodologies infrastructures. |
| **OPC UA (OPC Unified Architecture)** | Defines a fundamental system-level core data interoperability framework interactions standard protocol matching next-generation industrial automation domains emphasizing deployments mapping cross-platform secure architectural designs principles implementations methodologies (functioning entirely compliant against overarching IEC 62541 core standard series frameworks documentation). |
| **Data Block (DB Structural Standard Definition Component)** | Distinguishes core infrastructural component logic blocks situated strictly within native Siemens S7 Programmable Logic Controller (PLC) operational environments memory structures, primarily fulfilling specialized functions targeting persisting structural process properties parameters data arrays retaining configurations mappings metrics long-term storage mechanisms blocks. |
| **Big-Endian (Communications Storage Topologies Modeling Strategy)** | Distinguishes ubiquitous implementation structural binary architecture array methodologies universally dominating foundational low-level digital industrial operations encodings transformations scenarios uniformly mandating deployments indexing highest-magnitude sequence parameters (Most Significant Byte MSB properties values) orienting memory mapping allocations priorities strictly occupying lowest corresponding addressing indices structures models definitions mappings. |
| **NodeId (Communications Identifications Node Handle Variables)** | Systemically embedded properties metrics introduced across standardized broad-scale OPC UA semantic spatial map environments frameworks distributing precisely directed individual functionalities allocating unrepeatable unique validation configurations mechanisms yielding standard identification credentials variables handles granting complete target node interaction authorizations accessing operations metrics tokens assignments properties variables schemas layouts maps matrices mapping operations structures mapping. |
| **Quality Code (Communications Systems Data Integrity Indices Tags Arrays)** | Evaluated indices markers fundamentally incorporated beneath full OPC UA architectural frameworks specification guidelines sets mapping mechanisms systematically generating diagnostic assessments analyzing sampled input vectors mapping data origins validations verifications metrics combining hardware signal stability assessments indicators measuring transmissions parameters assigning standard evaluation benchmarks metrics appending resulting properties output functions logic supporting fundamental validation operations variables mapping metrics structures evaluations validations models algorithms properties schemas models operations indices values algorithms lists. |
