#!/usr/bin/env bash
# =============================================================================
# 构建脚本: RK3568 (正点原子 ATK-DLRK3568) aarch64 交叉编译
#
# 用法 A -- 板子是 ALIENTEK Buildroot 镜像 / Debian 12 (glibc >= 2.34):
#   ./scripts/build_rk3568.sh
#
# 用法 B -- 板子是 Debian 10/11 (glibc < 2.34),方案一用板子 sysroot:
#   ./scripts/bootstrap_sysroot_rk3568.sh          # 先 rsync 板子 -> /opt/rk3568_board_sysroot
#   ./scripts/build_rk3568.sh --sysroot /opt/rk3568_board_sysroot
#
# 清理重建:
#   ./scripts/build_rk3568.sh --clean
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$SRC_DIR/build_rk3568"
TOOLCHAIN_FILE="$SRC_DIR/cmake/aarch64-linux-gnu.cmake"

SYSROOT=""
CLEAN=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --sysroot)  SYSROOT="$2"; shift 2 ;;
        --clean)    CLEAN=1; shift ;;
        *) echo "未知参数: $1" >&2; exit 2 ;;
    esac
done

# 用真实 cmake,避免 source 了 environment-setup 后 cmake 被 alias 劫持
CMAKE="$(command -v cmake 2>/dev/null || true)"
if [[ -z "$CMAKE" || "$CMAKE" == "cmake" ]]; then CMAKE=/usr/bin/cmake; fi

if [[ "$CLEAN" -eq 1 ]]; then
    echo ">> 清理 $BUILD_DIR"
    rm -rf "$BUILD_DIR"
fi

EXTRA=()
if [[ -n "$SYSROOT" ]]; then
    echo ">> 方案一: 使用板子 sysroot = $SYSROOT"
    EXTRA+=(-DRK3568_SYSROOT="$SYSROOT")
else
    echo ">> 用法 A: 官方工具链 sysroot (glibc 2.36, 产物要求 GLIBC_2.34)"
    echo "   若板子是 Debian 10/11 (glibc<2.34),请先运行 bootstrap_sysroot_rk3568.sh,"
    echo "   再加 --sysroot /opt/rk3568_board_sysroot 重新运行本脚本。"
fi

echo ">> 配置"
"$CMAKE" -S "$SRC_DIR" -B "$BUILD_DIR" \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
    -DCMAKE_BUILD_TYPE=Release \
    "${EXTRA[@]}"

echo ">> 构建"
"$CMAKE" --build "$BUILD_DIR" -j"$(nproc)"

BIN="$BUILD_DIR/ps_tracker_ui"
TC=/opt/atk-dlrk3568-5_10_sdk-toolchain
echo
echo ">> 产物: $BIN"
file "$BIN"
echo ">> 所需最高 GLIBC 符号 (板上 ldd --version 必须不低于此项):"
"$TC/bin/aarch64-buildroot-linux-gnu-readelf" -V "$BIN" 2>/dev/null \
    | grep -oE 'GLIBC_[0-9.]+' | sort -V | uniq | tail -3
echo
echo ">> 部署到板子:"
echo "   scp $BIN root@<板子IP>:/root/"
echo "   (Qt5 运行库若板子没装,见 docs/cross-compile-rk3568.md 的部署章节)"
