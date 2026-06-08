#!/usr/bin/env bash
# =============================================================================
#  构建 S7_to_OpenUA 多架构容器镜像
#
#  容器镜像只能是 Linux。本脚本构建 linux/amd64 + linux/arm64 两个变体，
#  合成一个多架构镜像 —— 它能在 Linux x86 / Linux ARM / macOS(经 Docker Desktop)
#  上运行。注意没有"macOS 镜像"这种东西，Mac 是跑 linux/arm64 那个变体。
#
#  用法：
#    ./docker-build.sh                      # 只构建【本机架构】并加载到本地，方便马上 docker run 测试
#    ./docker-build.sh --multi              # 构建 amd64+arm64 多架构(只能导出，不能 --load)
#    ./docker-build.sh --push <仓库地址:tag> # 构建 amd64+arm64 并推送到镜像仓库
#
#  例：
#    ./docker-build.sh
#    ./docker-build.sh --push registry.example.com/me/s7-opcua:1.0
# =============================================================================
set -euo pipefail

IMAGE="s7-opcua:latest"
MODE="local"          # local | multi | push
PUSH_REF=""
BUILDER="s7builder"
BUILD_ARGS=()

case "${1:-}" in
  --multi) MODE="multi" ;;
  --push)  MODE="push"; PUSH_REF="${2:?用法: ./docker-build.sh --push <仓库地址:tag>}"; IMAGE="$PUSH_REF" ;;
  "" )     MODE="local" ;;
  * )      IMAGE="$1" ;;     # 自定义本地 tag
esac

if ! command -v docker >/dev/null 2>&1; then
  echo "✗ 没找到 docker。请先安装 Docker Desktop(Mac) 或 Docker Engine(Linux)。" >&2
  exit 1
fi

# 如果本机开着 Shadowrocket/Clash 这类代理，并监听 1082 端口，
# 让 Docker 的构建容器通过 host.docker.internal:1082 走代理。
# 也可以手动覆盖：DOCKER_BUILD_PROXY=http://host.docker.internal:1082 ./docker-build.sh
if [[ -z "${DOCKER_BUILD_PROXY:-}" ]] && command -v lsof >/dev/null 2>&1; then
  if lsof -nP -iTCP:1082 -sTCP:LISTEN >/dev/null 2>&1; then
    DOCKER_BUILD_PROXY="http://host.docker.internal:1082"
  fi
fi

if [[ -n "${DOCKER_BUILD_PROXY:-}" ]]; then
  BUILDER="s7builder-proxy"
  BUILD_ARGS+=(--build-arg "http_proxy=$DOCKER_BUILD_PROXY")
  BUILD_ARGS+=(--build-arg "https_proxy=$DOCKER_BUILD_PROXY")
  echo "==> 检测到构建代理: $DOCKER_BUILD_PROXY"
fi

# 跨架构构建需要 QEMU 模拟(在 amd64 机上编 arm64，或反之)。装一次即可。
echo "==> 安装/确认 QEMU 多架构支持..."
docker run --privileged --rm tonistiigi/binfmt --install all >/dev/null 2>&1 || true

# 创建一个支持多平台的 buildx 构建器
if ! docker buildx inspect "$BUILDER" >/dev/null 2>&1; then
  echo "==> 创建 buildx 构建器 $BUILDER..."
  if [[ -n "${DOCKER_BUILD_PROXY:-}" ]]; then
    docker buildx create --name "$BUILDER" --driver docker-container \
      --driver-opt "env.http_proxy=$DOCKER_BUILD_PROXY" \
      --driver-opt "env.https_proxy=$DOCKER_BUILD_PROXY" \
      --use >/dev/null
  else
    docker buildx create --name "$BUILDER" --driver docker-container --use >/dev/null
  fi
else
  docker buildx use "$BUILDER"
fi

case "$MODE" in
  local)
    ARCH="$(uname -m)"
    case "$ARCH" in
      arm64|aarch64) PLAT="linux/arm64" ;;
      x86_64|amd64)  PLAT="linux/amd64" ;;
      *)             PLAT="linux/amd64" ;;
    esac
    echo "==> 构建本机架构 ($PLAT) 并加载到本地 docker: $IMAGE"
    docker buildx build --platform "$PLAT" -t "$IMAGE" --load "${BUILD_ARGS[@]}" .
    echo "✓ 完成。运行: docker run --rm -p 4840:4840 $IMAGE"
    ;;
  multi)
    echo "==> 构建多架构 linux/amd64,linux/arm64 (导出到本地镜像存储)"
    # 多架构镜像无法 --load 进普通 docker，这里用 oci 归档导出
    docker buildx build --platform linux/amd64,linux/arm64 \
      -t "$IMAGE" --output type=oci,dest=s7-opcua-multiarch.tar "${BUILD_ARGS[@]}" .
    echo "✓ 完成 -> s7-opcua-multiarch.tar (多架构 OCI 归档)"
    ;;
  push)
    echo "==> 构建并推送多架构 linux/amd64,linux/arm64 -> $PUSH_REF"
    docker buildx build --platform linux/amd64,linux/arm64 \
      -t "$PUSH_REF" --push "${BUILD_ARGS[@]}" .
    echo "✓ 已推送: $PUSH_REF"
    ;;
esac
