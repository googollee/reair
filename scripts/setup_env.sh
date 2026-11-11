#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Googol Lee
# 准备固件的编译环境:ESP-IDF(只装 esp32c3 工具链)和 esp-matter,都用浅克隆省空间。
# 已经装好的步骤会跳过,可以重复运行。由 make env 调用,路径和版本由 Makefile 传入:
#   IDF_DIR  ESP_MATTER  IDF_BRANCH  ESP_MATTER_BRANCH
# 可选 IDF_PY_ENV:不传则按「IDF 版本 + python3 版本」推算(IDF 克隆之前算不出版本号,所以在这里算)
# Python 环境跟着 PATH 里的 python3 走:IDF 的虚拟环境按 Python 小版本分开放。
set -euo pipefail

: "${IDF_DIR:?}" "${ESP_MATTER:?}" "${IDF_BRANCH:?}" "${ESP_MATTER_BRANCH:?}"

log() { echo "==> $*"; }

for t in git cmake ninja python3; do
  command -v "$t" >/dev/null || { echo "缺少 $t(macOS: brew install $t)"; exit 1; }
done
python3 -c 'import sys; sys.exit(sys.version_info < (3, 10))' \
  || { echo "ESP-IDF 需要 Python 3.10 或更高,当前 python3 是 $(python3 --version)"; exit 1; }

if [ ! -d "$IDF_DIR" ]; then
  log "克隆 ESP-IDF $IDF_BRANCH"
  mkdir -p "$(dirname "$IDF_DIR")"
  git clone -b "$IDF_BRANCH" --depth 1 --shallow-submodules --recursive \
    https://github.com/espressif/esp-idf.git "$IDF_DIR"
fi

if [ -z "${IDF_PY_ENV:-}" ]; then
  idf_ver=$(sed -nE 's/^set\(IDF_VERSION_(MAJOR|MINOR) ([0-9]+)\)/\2/p' "$IDF_DIR/tools/cmake/version.cmake" | head -2 | paste -sd. -)
  py_ver=$(python3 -c 'import sys; print("%d.%d" % sys.version_info[:2])')
  IDF_PY_ENV="$HOME/.espressif/python_env/idf${idf_ver}_py${py_ver}_env"
fi

if [ ! -x "$IDF_PY_ENV/bin/python" ]; then
  log "安装 ESP-IDF 工具链和 Python 环境($IDF_PY_ENV)"
  (cd "$IDF_DIR" && ./install.sh esp32c3)
fi

CHIP="$ESP_MATTER/connectedhomeip/connectedhomeip"
switched=0

if [ ! -d "$ESP_MATTER/.git" ]; then
  log "克隆 esp-matter $ESP_MATTER_BRANCH"
  mkdir -p "$(dirname "$ESP_MATTER")"
  git clone -b "$ESP_MATTER_BRANCH" --depth 1 https://github.com/espressif/esp-matter.git "$ESP_MATTER"
elif [ "$(git -C "$ESP_MATTER" rev-parse --abbrev-ref HEAD)" != "$ESP_MATTER_BRANCH" ]; then
  # 已有的克隆不在指定分支上:切过去,子模块版本会变,旧的主机工具要重新编译
  log "把 esp-matter 切到 $ESP_MATTER_BRANCH"
  git -C "$ESP_MATTER" fetch --depth 1 origin "$ESP_MATTER_BRANCH"
  git -C "$ESP_MATTER" checkout -B "$ESP_MATTER_BRANCH" FETCH_HEAD
  git -C "$ESP_MATTER" submodule update --init --depth 1
  rm -rf "$CHIP/out"
  switched=1
fi

# pigweed 是 checkout_submodules.py 检出的子模块之一,用它判断是否已经检出过;切换分支后要重新检出
if [ "$switched" = 1 ] || [ ! -e "$CHIP/third_party/pigweed/repo/.git" ]; then
  log "检出 connectedhomeip(只取 esp32 和本机平台需要的子模块)"
  (cd "$ESP_MATTER" && git submodule update --init --depth 1)
  (cd "$CHIP" \
    && ./scripts/checkout_submodules.py --shallow --platform esp32 "$(uname | tr 'A-Z' 'a-z')")
fi

matter_ready() {
  "$IDF_PY_ENV/bin/python" -c 'import esp_secure_cert' 2>/dev/null \
    && [ -x "$CHIP/out/host/chip-cert" ]
}

install_matter() {
  # esp-matter 的 install.sh 用当前激活的 IDF 环境装 Python 依赖,并编译 chip-cert 等主机工具
  (set +u; . "$IDF_DIR/export.sh" >/dev/null && cd "$ESP_MATTER" && ./install.sh)
}

# Apple 芯片没有 Rosetta 时,x86_64 的 zap-cli 跑不起来;换成官方 release 的 arm64 版
fix_zap_arm64() {
  [ "$(uname -s)-$(uname -m)" = "Darwin-arm64" ] || return 1
  local dir="$CHIP/.environment/cipd/packages/zap"
  [ -x "$dir/zap-cli" ] || return 1
  file "$dir/zap-cli" | grep -q x86_64 || return 1
  local tag
  # cipd 的版本标签(如 v2026.09.09.2、v2026.05.12-nightly.2)比 GitHub release 标签多一个构建序号
  tag=$(sed -n 's/.*"version:\([^"]*\)".*/\1/p' "$CHIP/scripts/setup/zap.json" | head -1 \
    | sed -E 's/(-nightly)\.[0-9]+$/\1/; s/^(v[0-9]{4}\.[0-9]{2}\.[0-9]{2})\.[0-9]+$/\1/')
  [ -n "$tag" ] || return 1
  log "把 zap-cli 换成官方 arm64 版($tag)"
  local tmp
  tmp=$(mktemp -d)
  curl -fL -o "$tmp/zap.zip" "https://github.com/project-chip/zap/releases/download/$tag/zap-mac-arm64.zip"
  unzip -q "$tmp/zap.zip" -d "$tmp/zap"
  xattr -cr "$tmp/zap" 2>/dev/null || true
  rm -rf "$dir"
  mkdir -p "$dir"
  cp -R "$tmp/zap/." "$dir/"
  rm -rf "$tmp"
}

if ! matter_ready; then
  log "安装 esp-matter 的 Python 依赖并编译主机工具"
  if ! install_matter; then
    fix_zap_arm64 || { echo "esp-matter 安装失败"; exit 1; }
    install_matter
  fi
fi

matter_ready || { echo "esp-matter 环境仍不完整"; exit 1; }
log "环境就绪: $IDF_PY_ENV"
