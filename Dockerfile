# =============================================================================
#  S7_to_OpenUA 网关容器镜像（多架构：linux/amd64 + linux/arm64）
#
#  说明：容器只能是 Linux 镜像。在 Apple Silicon Mac 上由 Docker Desktop
#  自动拉取并运行 linux/arm64 这个变体（Docker 内部跑的是 Linux 虚拟机）。
#  所以一个 amd64+arm64 的多架构镜像，就同时覆盖了 Linux x86 / Linux ARM /
#  macOS(ARM) 三种机器。
#
#  构建（多架构，见 docker-build.sh）：
#    docker buildx build --platform linux/amd64,linux/arm64 -t s7-opcua:latest .
# =============================================================================

# ---------- 第一阶段：构建 ----------
FROM debian:bookworm-slim AS builder

# 把 Debian 源换成国内镜像(阿里云)：默认的 deb.debian.org 在国内/代理环境下
# 经常下载失败(EOF)。国内源走直连，稳很多。
RUN sed -i 's|deb.debian.org|mirrors.aliyun.com|g; s|security.debian.org|mirrors.aliyun.com|g' \
        /etc/apt/sources.list.d/debian.sources 2>/dev/null || \
    sed -i 's|deb.debian.org|mirrors.aliyun.com|g; s|security.debian.org|mirrors.aliyun.com|g' \
        /etc/apt/sources.list 2>/dev/null || true

# 构建依赖：编译器、cmake、git、下载/解压工具、cJSON 开发包
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake git ca-certificates wget p7zip-full \
        libcjson-dev python3 \
    && rm -rf /var/lib/apt/lists/*

# ---- 1) 在 Linux 上从源码编译 snap7 成 libsnap7.so ----
#    （Linux 比 mac 简单：源码自带 Linux makefile 思路，这里直接手动编，加 -lrt）
WORKDIR /build
RUN wget -q -O snap7.7z \
      "https://master.dl.sourceforge.net/project/snap7/1.4.2/snap7-full-1.4.2.7z?viasf=1" \
    && 7z x snap7.7z >/dev/null \
    && cd snap7-full-1.4.2/src \
    && g++ -O3 -fPIC -shared -Isys -Icore -Ilib \
         sys/*.cpp core/*.cpp lib/*.cpp \
         -lpthread -lrt -o /usr/local/lib/libsnap7.so \
    && ldconfig

# ---- 2) 从源码编译 open62541（用 1.5.x，与本项目用的 config->logging API 匹配）----
#    若该 tag 拉取失败，把 v1.5.4 改成可用的 1.5.x tag 即可。
RUN git clone --depth 1 --branch v1.5.4 https://github.com/open62541/open62541.git \
    && cd open62541 && mkdir build && cd build \
    && cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON \
             -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF -DUA_ENABLE_LTO=OFF \
             -DUA_ENABLE_AMALGAMATION=OFF .. \
    && make -j1 && make install && ldconfig

# ---- 3) 编译网关本体 ----
WORKDIR /app
COPY . .
# 用我们项目里(打过补丁的) snap7.h + 刚装好的 open62541/cJSON 链接
RUN gcc -D_GNU_SOURCE -Wall -Wextra -std=c11 \
        -Iinclude -Ithird_party/snap7/include -I/usr/local/include -I/usr/include \
        src/main.c src/config.c src/s7_types.c src/s7_client.c \
        src/tag_cache.c src/opcua_server.c \
        -L/usr/local/lib -lsnap7 -lopen62541 -lcjson -lpthread -lm \
        -o /app/gateway

# ---------- 第二阶段：运行 ----------
FROM debian:bookworm-slim

# 运行时依赖：snap7 是 C++ 编的(需 libstdc++)，cJSON 运行库；
# python3 + python3-openpyxl 给容器内的 xlsx 点表转换用(用 Debian 包，避开 pip/PEP668)。
RUN sed -i 's|deb.debian.org|mirrors.aliyun.com|g; s|security.debian.org|mirrors.aliyun.com|g' \
        /etc/apt/sources.list.d/debian.sources 2>/dev/null || \
    sed -i 's|deb.debian.org|mirrors.aliyun.com|g; s|security.debian.org|mirrors.aliyun.com|g' \
        /etc/apt/sources.list 2>/dev/null || true
RUN apt-get update && apt-get install -y --no-install-recommends \
        libcjson1 libstdc++6 python3 python3-openpyxl \
    && rm -rf /var/lib/apt/lists/*

# 拷贝构建产物：网关 + 两个自编译的 .so + open62541 装好的库 + 配置
COPY --from=builder /usr/local/lib/libsnap7.so        /usr/local/lib/
COPY --from=builder /usr/local/lib/libopen62541.so*   /usr/local/lib/
COPY --from=builder /app/gateway                       /app/gateway
COPY --from=builder /app/config                        /app/config
# xlsx 点表转换脚本 + 入口脚本（让容器既能吃 JSON、也能吃 xlsx）
COPY --from=builder /app/tools/xlsx_to_config.py       /app/tools/xlsx_to_config.py
COPY docker-entrypoint.sh                              /app/docker-entrypoint.sh
RUN chmod +x /app/docker-entrypoint.sh && ldconfig

WORKDIR /app
# OPC UA 默认端口（按需对外暴露）
EXPOSE 4840

# 入口脚本支持两种启动：直接给 JSON，或 --xlsx 点表自动转换后启动（见 docker-entrypoint.sh）。
# 默认用内置配置；真实部署时用 -v 挂载你自己的配置目录覆盖 /app/config。
ENTRYPOINT ["/app/docker-entrypoint.sh"]
CMD ["config/gateway.json"]
