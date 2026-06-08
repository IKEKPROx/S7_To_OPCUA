#!/usr/bin/env bash
# =============================================================================
#  容器入口脚本：支持两种启动方式
#
#   1) 直接给 JSON 配置（默认，和以前一样）：
#        docker run --rm -p 4840:4840 -v ./config:/app/config <镜像> config/gateway.json
#
#   2) 给 xlsx 点表，容器内【自动转换】成 JSON 再起网关：
#        docker run --rm -p 4840:4840 -v ./config:/app/config <镜像> \
#            --xlsx /app/config/points.xlsx --ip 192.168.0.10 --plc-name 炉区PLC
#      规则：--xlsx 后面紧跟 xlsx 路径，其余参数（--ip/--port/--plc-name/
#      --opcua-port/--cache-ttl-ms 等）原样传给转换脚本 tools/xlsx_to_config.py。
#
#  这些路径默认指向容器里的 /app；本地测试可用环境变量覆盖（见仓库 README）。
# =============================================================================
set -euo pipefail

GATEWAY_BIN="${GATEWAY_BIN:-/app/gateway}"
CONVERTER="${CONVERTER:-/app/tools/xlsx_to_config.py}"
PYTHON_BIN="${PYTHON_BIN:-python3}"
OUT_CONFIG="${GENERATED_CONFIG:-/tmp/gateway-from-xlsx.json}"

if [[ "${1:-}" == "--xlsx" ]]; then
  shift
  XLSX="${1:?用法: --xlsx <xlsx路径> [--ip ... 等转换参数]}"
  shift
  echo "==> [入口] 转换点表: $XLSX  ->  $OUT_CONFIG"
  "$PYTHON_BIN" "$CONVERTER" "$XLSX" -o "$OUT_CONFIG" "$@"
  echo "==> [入口] 用生成的配置启动网关"
  exec "$GATEWAY_BIN" "$OUT_CONFIG"
else
  # 默认：把参数原样交给网关（缺省 config/gateway.json）
  exec "$GATEWAY_BIN" "$@"
fi
