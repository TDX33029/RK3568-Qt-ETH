#!/usr/bin/env bash
# =============================================================================
# 方案: 在本机 debootstrap 一个 Debian 10 (buster) arm64 chroot, 并在其中
#       用 qemu 原生构建本项目 -> 产物只需 glibc 2.28, 可在 RK3568 Debian10 上运行。
#
# 为什么是这个方法:
#   - 不用 SSH / 不碰板子(SSH 账号验证过不去也能用)
#   - 不用正点原子官方工具链(它 sysroot 是 glibc 2.36, 产物要求 GLIBC_2.34,
#     Debian 10/11 给不了)
#   - 不用任何跨编译器(跨编译器自带的 libstdc++/crt 会和新 sysroot 冲突)
#   - chroot 里全是 buster 原生(gcc-8 + libstdc++ + Qt5 5.11.3 + glibc 2.28),
#     绝无版本错配
#
# 前置: sudo(装包/debootstrap/chroot)、网络、qemu-user-static(已装且 binfmt 已注册)。
# 耗时: 首次约 10~20 分钟(debootstrap + apt 在 qemu 下较慢), 之后构建约 2~3 分钟。
#
# 用法:
#   sudo bash scripts/build_debian10_chroot.sh            # 首次: 建chroot+装Qt5+构建
#   sudo bash scripts/build_debian10_chroot.sh --build-only # 之后只重新构建
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
CHROOT_DIR=${RK3568_CHROOT:-/opt/rk3568_buster_chroot}
MIRROR=http://archive.debian.org/debian   # 仅用于 debootstrap 构建 chroot 基础系统, 非板子源
SUITE=buster
BIN_OUT="$SRC_DIR/build_debian10/ps_tracker_ui"

BUILD_ONLY=0
[[ "${1:-}" == "--build-only" ]] && BUILD_ONLY=1

[ "$(id -u)" -eq 0 ] || { echo "需要 root, 请用 sudo 运行" >&2; exit 1; }

# 清理: 卸载 chroot 内挂载点
cleanup() {
    for m in "$CHROOT_DIR/proc" "$CHROOT_DIR/sys" "$CHROOT_DIR/dev" "$CHROOT_DIR/dev/pts"; do
        mountpoint -q "$m" 2>/dev/null && umount "$m" 2>/dev/null || true
    done
}
trap cleanup EXIT

mount_chroot_fs() {
    mount --bind /proc "$CHROOT_DIR/proc"
    mount --bind /sys  "$CHROOT_DIR/sys"  2>/dev/null || true
    mount --bind /dev  "$CHROOT_DIR/dev"
    mount --bind /dev/pts "$CHROOT_DIR/dev/pts" 2>/dev/null || true
}

echo "===================================================="
echo " chroot: $CHROOT_DIR"
echo " 源码  : $SRC_DIR"
echo " 镜像  : $MIRROR ($SUITE)"
echo " build-only: $BUILD_ONLY"
echo "===================================================="

# ── 1. 装宿主侧工具 + keyring ──────────────────────────────────────────────
if [ "$BUILD_ONLY" -eq 0 ]; then
    echo ">> [1/4] 安装 debootstrap / qemu / keyring ..."
    apt-get update
    DEBIAN_FRONTEND=noninteractive apt-get install -y \
        debootstrap qemu-user-static binfmt-support debian-archive-keyring rsync
fi

# ── 2. debootstrap buster arm64 ────────────────────────────────────────────
if [ "$BUILD_ONLY" -eq 0 ]; then
    if [ ! -x "$CHROOT_DIR/bin/bash" ]; then
        echo ">> [2/4] debootstrap $SUITE arm64 (首次较慢) ..."
        # 两段式: --foreign 只下载解包(不跑 arm64 脚本); 第二阶段靠 binfmt 的 qemu 跑
        # --no-check-gpg: buster 已 EOL, 密钥过期/缺 gpgv 会报 "Error executing gpgv", 跳过校验
        debootstrap --arch=arm64 --no-check-gpg --foreign "$SUITE" "$CHROOT_DIR" "$MIRROR"
        chroot "$CHROOT_DIR" /debootstrap/debootstrap --second-stage
    else
        echo ">> [2/4] chroot 已存在, 跳过 debootstrap"
    fi

    # apt 源 + 关闭 buster 过期校验(EOL) + DNS
    echo "deb $MIRROR $SUITE main" > "$CHROOT_DIR/etc/apt/sources.list"
    mkdir -p "$CHROOT_DIR/etc/apt/apt.conf.d"
    printf 'Acquire::Check-Valid-Until "false";\n' > "$CHROOT_DIR/etc/apt/apt.conf.d/99no-valid-until"
    # resolv.conf: 拷贝内容(避免宿主是 systemd-resolved 软链)
    if [ -r /etc/resolv.conf ]; then cp -f --remove-destination /etc/resolv.conf "$CHROOT_DIR/etc/resolv.conf"
    else echo "nameserver 8.8.8.8" > "$CHROOT_DIR/etc/resolv.conf"; fi

    echo ">> [3/4] chroot 内 apt update + 安装构建依赖 (qemu 下较慢, 请耐心) ..."
    mount_chroot_fs
    chroot "$CHROOT_DIR" apt-get update
    DEBIAN_FRONTEND=noninteractive chroot "$CHROOT_DIR" apt-get install -y --no-install-recommends \
        build-essential cmake pkg-config \
        qtbase5-dev qtbase5-dev-tools qt5-qmake \
        libgl1-mesa-dev libegl1-mesa-dev libgles2-mesa-dev
    cleanup
fi

# ── 3. 把源码拷进 chroot 并原生构建 ────────────────────────────────────────
echo ">> [4/4] 同步源码到 chroot 并构建 ..."
mkdir -p "$CHROOT_DIR/src"
rsync -a --delete \
    --exclude '/build*/' --exclude '/backup_*/' --exclude '/.qtcreator/' \
    --exclude '/sysroot*/' \
    "$SRC_DIR/" "$CHROOT_DIR/src/"

mount_chroot_fs
# chroot 内构建 (gcc-8 + Qt5 5.11.3, 全 buster 原生, 经 qemu 执行)
chroot "$CHROOT_DIR" bash -c "
    set -e
    cd /src
    rm -rf build_chroot
    cmake -S . -B build_chroot -DCMAKE_BUILD_TYPE=Release -DQT_PREFIX=/usr
    cmake --build build_chroot -- -j\$(nproc)
    strip build_chroot/ps_tracker_ui || true
"
cleanup

# ── 4. 取出产物 ────────────────────────────────────────────────────────────
mkdir -p "$(dirname "$BIN_OUT")"
cp "$CHROOT_DIR/src/build_chroot/ps_tracker_ui" "$BIN_OUT"

echo
echo ">> 产物: $BIN_OUT"
file "$BIN_OUT"
echo ">> 产物所需最高 GLIBC (板上 ldd --version 须不低于此项, Debian10=2.28 应满足):"
"$CHROOT_DIR"/bin/readelf -V "$BIN_OUT" 2>/dev/null | grep -oE 'GLIBC_[0-9.]+' | sort -V | uniq | tail -3 \
    || readelf -V "$BIN_OUT" 2>/dev/null | grep -oE 'GLIBC_[0-9.]+' | sort -V | uniq | tail -3 || true
echo
echo ">> 部署到板子:"
echo "   scp $BIN_OUT root@<板子IP>:/root/"
echo "   (板子 Debian 10, Qt5 运行库: apt install libqt5widgets5)"
