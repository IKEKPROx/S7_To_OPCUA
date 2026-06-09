#!/usr/bin/env bash
#
# 真链路冒烟：起 fake_plc + gateway + ua_monitor 三进程，确认整条链路连通、
# 所有点 Good、数值在动。用于 0.1.2 批量读改动的端到端验证。
#
#   用法： scripts/bench_batch.sh
#
# 说明：
#   - macOS 没有 GNU `timeout`，这里用「后台 + sleep + kill」限时。
#   - 本脚本做功能冒烟。批量读的“趟数”硬证据见 tests/test_opcua_server.c 的
#     场景 B（读一个节点→同台 PLC 所有点一并刷新）与场景 C（batch_read=0 退回逐点），
#     `make test` 即可复跑。
#   - 若想在真链路里亲眼看 PLC 往返趟数：在 src/s7_client.c 的 s7_conn_read_many
#     的 Cli_ReadMultiVars 前、s7_conn_read_tag 的 Cli_ReadArea 前各加一行
#     `fprintf(stderr, "...")` 计数，重编后对比 batch_read 0/1 两种配置即可：
#       实测 11 点 → batch_read=1 每轮 1 趟 ReadMultiVars；=0 每轮 11 趟 ReadArea。
#
set -u
cd "$(dirname "$0")/.." || exit 1

PORT=4840
CFG=config/fake_plc.json

echo "[1/4] 构建 gateway / fake_plc / ua_monitor ..."
make gateway fake_plc ua_monitor >/tmp/bench_build.log 2>&1 || { echo "  构建失败，见 /tmp/bench_build.log"; exit 1; }

cleanup() { kill ${MON:-0} ${GW:-0} 2>/dev/null; pkill -f '/fake_plc' 2>/dev/null; }
trap cleanup EXIT
pkill -f '/fake_plc' 2>/dev/null; pkill -f '/gateway' 2>/dev/null; sleep 1

echo "[2/4] 启动假 PLC ..."
./fake_plc >/tmp/bench_fake.log 2>&1 &
sleep 1.5

echo "[3/4] 启动网关并等待 OPC UA 监听 :$PORT ..."
./gateway "$CFG" >/tmp/bench_gw.log 2>&1 &
GW=$!
for i in $(seq 1 120); do
  lsof -nP -iTCP:$PORT -sTCP:LISTEN >/dev/null 2>&1 && break
  sleep 0.5
done
echo "  已监听（约 $((i/2))s）"

echo "[4/4] 用 ua_monitor 读取约 6 秒 ..."
./ua_monitor "$CFG" >/tmp/bench_mon.log 2>&1 &
MON=$!
sleep 6
kill $MON 2>/dev/null; sleep 0.5

echo
echo "=== ua_monitor 末屏（应为各点最新值 + Good） ==="
tr -d '\033' < /tmp/bench_mon.log | sed 's/\[[0-9;?]*[A-Za-z]//g' | grep -aE "Good|BAD" | tail -12
good=$(tr -d '\033' < /tmp/bench_mon.log | grep -ac Good)
bad=$(tr -d '\033' < /tmp/bench_mon.log | grep -ac BAD)
echo
echo "汇总: Good 行计数=${good}, BAD 行计数=${bad} (链路正常时 BAD 应为 0)"
