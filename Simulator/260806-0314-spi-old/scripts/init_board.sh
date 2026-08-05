#!/usr/bin/env bash
# =============================================================================
#  RK3568 板上一键初始化 (在板子上运行, 原生 Debian)
#    1) 装原生构建依赖 (build-essential / cmake / qtbase5-dev ...)
#    2) SPI1 设备检查 + /dev/spidev1.0 读写权限 (udev 规则)
#    3) 原生构建项目 (cmake + make, 产物 build/ps_tracker_ui)
#    4) 打印运行命令
#
#  用法:  bash scripts/init_board.sh
#  (脚本内部用 sudo 做 apt/udev/chmod, 会提示密码; 构建以当前用户身份进行)
#
#  说明: 设备树启用 SPI1 这一步因板型而异, 本脚本不自动改设备树, 仅检测并提示。
#        若你更想要 glibc 2.28 的产物, 改用 scripts/build_debian10_chroot.sh。
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
SPI_DEV=/dev/spidev1.0
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
c_hi "[1/4] 检查/安装原生构建依赖"
$SUDO apt-get update -qq
$SUDO DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    build-essential cmake pkg-config g++ \
    qtbase5-dev qtbase5-dev-tools qt5-qmake \
    libgl1-mesa-dev libegl1-mesa-dev libgles2-mesa-dev
c_ok "构建依赖就绪"

# ── 2. SPI1 设备 + 权限 ───────────────────────────────────────────────────
echo
c_hi "[2/4] SPI1 设备检查 + 权限"
if [ -c "$SPI_DEV" ]; then
    c_ok "$SPI_DEV 已存在"
    # 看一下当前权限/属主
    ls -l "$SPI_DEV"
else
    c_no "$SPI_DEV 不存在! 需在设备树启用 SPI1:"
    echo "    - RK3568 正点原子镜像: 检查 /boot 下 dtb / overlay, 把 spi1 节点 status 改 'okay'"
    echo "    - 或编辑设备树源: 找 spi1@...{ status = \"okay\"; ... } 后重新编译 dtb"
    echo "    - 改完重启板子, 再跑本脚本"
    echo "    (本脚本不自动改设备树, 避免损坏启动配置)"
fi

# udev 规则: spidev 全局可读写 (开发板; 生产环境请改用 group+MODE 0660)
UDEV_RULE=/etc/udev/rules.d/99-spidev.rules
if [ ! -f "$UDEV_RULE" ] || ! grep -q 'SUBSYSTEM=="spidev"' "$UDEV_RULE" 2>/dev/null; then
    echo 'SUBSYSTEM=="spidev", MODE="0666"' | $SUDO tee "$UDEV_RULE" >/dev/null
    $SUDO udevadm control --reload-rules 2>/dev/null || true
    $SUDO udevadm trigger --subsystem-match=spidev 2>/dev/null || true
    c_ok "已写入 udev 规则 $UDEV_RULE (spidev 0666)"
else
    c_ok "udev 规则已存在"
fi
# 立即生效(本次会话)
[ -c "$SPI_DEV" ] && $SUDO chmod 666 "$SPI_DEV" 2>/dev/null || true
[ -c "$SPI_DEV" ] && c_ok "$SPI_DEV 权限 OK" || c_no "SPI1 设备仍不可用 (先解决设备树)"

# ── 3. 原生构建 ─────────────────────────────────────────────────────────────
echo
c_hi "[3/4] 原生构建 (cmake + make, 当前用户身份)"
cd "$SRC_DIR"
CMAKE_BIN="$(command -v cmake || echo cmake)"
# 用系统 Qt5; cmake >= 3.13 即可 (本机 CMakeLists 已降到 3.13)
"$CMAKE_BIN" -B "$BUILD_DIR" -S . -DCMAKE_PREFIX_PATH=/usr -DQT_PREFIX=/usr -DCMAKE_BUILD_TYPE=Release
# cmake 3.13 不认合并 -j4, 用 -- -j 交给 make
"$CMAKE_BIN" --build "$BUILD_DIR" -- -j"$(nproc)"
BIN="$BUILD_DIR/ps_tracker_ui"
if [ -x "$BIN" ]; then c_ok "构建成功: $BIN"; else c_no "构建失败"; exit 1; fi
file "$BIN" 2>/dev/null || true

# ── 4. 运行提示 ─────────────────────────────────────────────────────────────
echo
c_hi "[4/4] 完成"
echo "  产物  : $BIN"
echo "  运行  : $BIN"
echo "  (需图形环境; 板子本机屏幕直接跑; SSH 远程加 -platform offscreen 仅测试接收)"
echo
echo "  SPI 校验: 点 Start Live (SPI) 后, 若一直 'magic/crc 不符',"
echo "            多半是字节序或帧对齐; 在传感器端打印前 16 字节核对。"
echo "  想要 glibc 2.28 产物: 改用 scripts/build_debian10_chroot.sh"
