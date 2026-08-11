#!/usr/bin/env bash
# =============================================================================
#  RK3568 板上一键初始化 (在板子上运行, 原生 Debian)
#    1) 装原生构建依赖 (build-essential / cmake / qtbase5-dev ...)
#    2) 原生构建项目 (cmake + make, 产物 build/ps_tracker_ui)
#    3) 打印运行命令
#
#  用法:  bash scripts/init_board.sh
#  (脚本内部用 sudo 做 apt, 会提示密码; 构建以当前用户身份进行)
#
#  说明: 本项目数据链路走 ETH1 (UDP), 不使用 SPI, 故本脚本无 SPI 设备/设备树
#        相关步骤。若你更想要 glibc 2.28 的产物, 改用 scripts/build_debian10_chroot.sh。
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$SRC_DIR/build"

c_ok(){ printf "\033[32m[OK]\033[0m %s\n" "$1"; }
c_no(){ printf "\033[31m[NO]\033[0m %s\n" "$1"; }
c_hi(){ printf "\033[1;36m== %s ==\n\033[0m" "$1"; }

c_hi "RK3568 板上初始化  (源码: $SRC_DIR)"

# ── 0. sudo 缓存 ────────────────────────────────────────────────────────────
if [ "$(id -u)" -ne 0 ]; then
    sudo -v || { c_no "需要 sudo 权限"; exit 1; }
    SUDO=sudo
else
    SUDO=""
fi

# ── 1. 构建依赖 ────────────────────────────────────────────────────────────
echo
c_hi "[1/3] 检查/安装原生构建依赖"
# 说明: 本脚本不自动修改 apt 源。若板子为不再支持的 Debian 10 (buster, EOL),
#       其主源已撤会导致 apt-get update 失败, 请先运行 scripts/fix_debian10_apt.sh
#       修复源, 再回来执行本初始化。
$SUDO apt-get update -qq
$SUDO env DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    build-essential cmake pkg-config g++ \
    qtbase5-dev qtbase5-dev-tools qt5-qmake \
    libgl1-mesa-dev libegl1-mesa-dev libgles2-mesa-dev
c_ok "构建依赖就绪"

# ── 2. 原生构建 ─────────────────────────────────────────────────────────────
echo
c_hi "[2/3] 原生构建 (cmake + make, 当前用户身份)"
cd "$SRC_DIR"
CMAKE_BIN="$(command -v cmake || echo cmake)"
# 若 build/ 是从别处拷来的陈旧 cache (源码路径不一致), 清掉重来
if [ -f "$BUILD_DIR/CMakeCache.txt" ]; then
    CACHE_HOME=$(grep -m1 '^CMAKE_HOME_DIRECTORY:' "$BUILD_DIR/CMakeCache.txt" 2>/dev/null | cut -d= -f2-)
    if [ -n "$CACHE_HOME" ] && [ "$CACHE_HOME" != "$SRC_DIR" ]; then
        echo ">> 检测到陈旧 build cache (来自 $CACHE_HOME), 清理后重新配置..."
        rm -rf "$BUILD_DIR"
    fi
fi
# 用系统 Qt5; cmake >= 3.13 即可 (本机 CMakeLists 已降到 3.13)
"$CMAKE_BIN" -B "$BUILD_DIR" -S . -DCMAKE_PREFIX_PATH=/usr -DQT_PREFIX=/usr -DCMAKE_BUILD_TYPE=Release
# cmake 3.13 不认合并 -j4, 用 -- -j 交给 make
"$CMAKE_BIN" --build "$BUILD_DIR" -- -j"$(nproc)"
BIN="$BUILD_DIR/ps_tracker_ui"
if [ -x "$BIN" ]; then c_ok "构建成功: $BIN"; else c_no "构建失败"; exit 1; fi
file "$BIN" 2>/dev/null || true

# ── 3. 运行提示 ─────────────────────────────────────────────────────────────
echo
c_hi "[3/3] 完成"
echo "  产物  : $BIN"
echo "  运行  : $BIN           (板子本机屏幕直接跑)"
echo "  接收测试: ./ethtest --verbose   (板端 C 程序, 通过 ETH1 UDP 收并显示数据)"
echo "  (需图形环境; SSH 远程可加 -platform offscreen 仅测试 Qt)"
echo
echo "  ETH1 校验: 点 Start Live (ETH1) 后, 若一直 'magic/crc 不符',"
echo "            多半是字节序或帧对齐; 在传感器端打印前 16 字节核对。"
echo "  想要 glibc 2.28 产物: 改用 scripts/build_debian10_chroot.sh"
