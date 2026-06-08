# 第一版 Makefile：构建网关程序(gateway) 和各层单元测试。
# 详见 README.md。等项目变大可换成 CMake。

CC      = clang
CFLAGS  = -Wall -Wextra -std=c11 -Iinclude

# Homebrew 安装路径（cJSON 等库在这里）
BREW    = /opt/homebrew
CJSON_CFLAGS = -I$(BREW)/include
CJSON_LIBS   = -L$(BREW)/lib -lcjson

# snap7（项目内自带，源码编译出来的）
SNAP7_DIR    = third_party/snap7
SNAP7_CFLAGS = -I$(SNAP7_DIR)/include
SNAP7_LIBS   = -L$(SNAP7_DIR)/lib -lsnap7 -Wl,-rpath,$(SNAP7_DIR)/lib

# open62541（brew 安装）
OPCUA_CFLAGS = -I$(BREW)/include
OPCUA_LIBS   = -L$(BREW)/lib -lopen62541

# 默认目标：构建网关可执行程序
all: gateway

GW_SRC = src/main.c src/config.c src/s7_types.c src/s7_client.c \
         src/tag_cache.c src/opcua_server.c

# 真正的网关程序：连真实 PLC -> 暴露为 OPC UA。
gateway: $(GW_SRC)
	$(CC) $(CFLAGS) $(CJSON_CFLAGS) $(SNAP7_CFLAGS) $(OPCUA_CFLAGS) \
	  $(GW_SRC) \
	  $(CJSON_LIBS) $(SNAP7_LIBS) $(OPCUA_LIBS) -lpthread -lm -o gateway
	@echo "构建完成 -> ./gateway   (用法: ./gateway [配置文件])"

# 假 PLC：用 snap7 server 模拟一台真实 PLC，灌入会变化的产线数据，方便本地联调。
fake_plc: tools/fake_plc.c
	$(CC) $(CFLAGS) $(SNAP7_CFLAGS) tools/fake_plc.c $(SNAP7_LIBS) -lpthread -lm -o fake_plc
	@echo "构建完成 -> ./fake_plc   (用法: ./fake_plc [端口，默认1102])"

# 终端版 OPC UA 监控面板（替代 UaExpert），每秒刷新显示所有点的值。
ua_monitor: tools/ua_monitor.c src/config.c src/s7_types.c
	$(CC) $(CFLAGS) $(CJSON_CFLAGS) $(OPCUA_CFLAGS) \
	  tools/ua_monitor.c src/config.c src/s7_types.c \
	  $(CJSON_LIBS) $(OPCUA_LIBS) -lpthread -lm -o ua_monitor
	@echo "构建完成 -> ./ua_monitor   (用法: ./ua_monitor [配置文件])"

# 跑全部测试
test: test_s7_types test_config test_s7_client test_tag_cache test_opcua_server

# s7_types 层：纯 C，不依赖任何外部库
test_s7_types: tests/test_s7_types.c src/s7_types.c
	$(CC) $(CFLAGS) $^ -o build_test_s7_types
	./build_test_s7_types

# config 层：依赖 cJSON
test_config: tests/test_config.c src/config.c src/s7_types.c
	$(CC) $(CFLAGS) $(CJSON_CFLAGS) $^ $(CJSON_LIBS) -o build_test_config
	./build_test_config

# s7_client 层：依赖 snap7。snap7 是 C++ 编的，但已是自包含 dylib，
# 我们的代码是纯 C，用 clang 编译链接即可（dylib 会自己带上 C++ 运行时）。
test_s7_client: tests/test_s7_client.c src/s7_client.c src/s7_types.c
	$(CC) $(CFLAGS) $(SNAP7_CFLAGS) $^ $(SNAP7_LIBS) -lpthread -o build_test_s7_client
	./build_test_s7_client

# tag_cache 层：纯 C + pthread
test_tag_cache: tests/test_tag_cache.c src/tag_cache.c
	$(CC) $(CFLAGS) $^ -lpthread -o build_test_tag_cache
	./build_test_tag_cache

# opcua_server 层：依赖 open62541 + snap7(按需采集要调 s7_client) + tag_cache + s7_types
test_opcua_server: tests/test_opcua_server.c src/opcua_server.c src/tag_cache.c src/s7_types.c src/s7_client.c
	$(CC) $(CFLAGS) $(OPCUA_CFLAGS) $(SNAP7_CFLAGS) $^ $(OPCUA_LIBS) $(SNAP7_LIBS) -lpthread -lm -o build_test_opcua_server
	./build_test_opcua_server

# 用 ThreadSanitizer 跑并发测试，直接抓数据竞争（手动跑：make tsan）。
# 注意：macOS 26 (Tahoe) + Apple clang 16 的 sanitizer 运行时当前有 bug，
# 连最小程序都会段错误——这是系统/工具链问题，不是本项目代码的问题。
# 在 Linux 或修好的工具链上可正常使用。日常以 test_tag_cache 的并发测试为准。
tsan: tests/test_tag_cache.c src/tag_cache.c
	@echo "提示: 若本机 sanitizer 运行时损坏(如 macOS 26)，此目标会段错误，属环境问题。"
	$(CC) $(CFLAGS) -fsanitize=thread -g $^ -lpthread -o build_test_tag_cache_tsan
	./build_test_tag_cache_tsan

clean:
	rm -rf gateway gateway.dSYM fake_plc fake_plc.dSYM ua_monitor ua_monitor.dSYM \
	       build_test_s7_types build_test_config build_test_s7_client \
	       build_test_tag_cache build_test_tag_cache_tsan \
	       build_test_opcua_server *.dSYM

.PHONY: all gateway fake_plc ua_monitor test test_s7_types test_config \
        test_s7_client test_tag_cache test_opcua_server tsan clean
