#!/usr/bin/env bash
# =============================================================================
# 方案一: 从 RK3568 板子拉取根文件系统, 构造交叉编译 sysroot
#
# 目的: 解决 "glibc version too low"。
#   官方 ALIENTEK 工具链 sysroot 是 glibc 2.36 -> 产物要求 GLIBC_2.34。
#   若板子是 Debian 10 (glibc 2.28) / Debian 11 (glibc 2.31), 直接编会报错。
#   把板子自己的 /lib /usr rsync 下来当 sysroot, 链接后产物只要求板子实际
#   拥有的符号 -> 能在板子上跑。
#
# 前置:
#   1. 板子已开机且能 SSH (默认 root@192.168.1.100, 用 --host 覆盖)
#   2. 本机已装 rsync
#   3. 若要用 Qt5: 板子上需 apt 安装 Qt5 开发包 (见下方 BOARD_QT)
#
# 用法:
#   ./scripts/bootstrap_sysroot_rk3568.sh --host root@192.168.1.100
#   # 若板子没装 Qt5 开发包,加 --install-qt (在板子上 apt 安装):
#   ./scripts/bootstrap_sysroot_rk3568.sh --host root@192.168.1.100 --install-qt
# =============================================================================
set -euo pipefail

HOST="root@192.168.1.100"
SYSROOT="/opt/rk3568_board_sysroot"
INSTALL_QT=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --host)       HOST="$2"; shift 2 ;;
        --sysroot)    SYSROOT="$2"; shift 2 ;;
        --install-qt) INSTALL_QT=1; shift ;;
        *) echo "未知参数: $1" >&2; exit 2 ;;
    esac
done

command -v rsync >/dev/null || { echo "请先安装 rsync: sudo apt install rsync"; exit 1; }

echo ">> 目标 sysroot: $SYSROOT"
echo ">> 板子: $HOST"
echo ">> 远程确认板子 glibc 版本:"
ssh "$HOST" 'ldd --version | head -1; cat /etc/os-release | grep -E "^(NAME|VERSION)=" ' || {
    echo "!! 无法 SSH 到 $HOST,请检查板子网络/地址。"; exit 1; }

if [[ "$INSTALL_QT" -eq 1 ]]; then
    echo ">> 在板子上安装 Qt5 开发包 (Debian/Ubuntu apt)..."
    ssh "$HOST" 'apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y \
        qtbase5-dev qtbase5-dev-tools libqt5gui5 libqt5widgets5 libqt5core5a \
        libgl1-mesa-dev libegl1-mesa-dev' || {
        echo "!! 板子上 apt 安装 Qt5 失败。可手动装好后再重跑本脚本(不带 --install-qt)。"; exit 1; }
fi

echo ">> 创建 sysroot 目录 ($SYSROOT) -- 需要 sudo"
sudo mkdir -p "$SYSROOT"
sudo chown -R "$(id -u):$(id -g)" "$SYSROOT"

# 关键目录: libc/libstdc++/libgcc/ld-linux + 头文件 + Qt
echo ">> rsync 板子文件系统到 $SYSROOT ..."
RSYNC_OPTS="-aHAX --info=progress2 --delete-excluded"
for path in \
    /lib/aarch64-linux-gnu \
    /usr/lib/aarch64-linux-gnu \
    /usr/include \
    /usr/share/pkgconfig \
    /usr/lib/pkgconfig \
    /lib/ld-linux-aarch64.so.1 ; do
    ssh "$HOST" "test -e $path" && \
        sudo rsync $RSYNC_OPTS "$HOST:$path" "$SYSROOT$(dirname $path)/" || \
        echo "   (跳过 $path, 板子上不存在)"
done
# Qt5 在 Debian 上常在 /usr/lib/aarch64-linux-gnu/cmake/Qt5 (已含在上面)
# Buildroot 镜像的 Qt 在 /usr/lib (也已含)

# 让 ld-linux 可被找到 (某些 sysroot 检索需要)
sudo ln -sf lib/aarch64-linux-gnu/ld-linux-aarch64.so.1 "$SYSROOT/ld-linux-aarch64.so.1" 2>/dev/null || true

echo ">> 校验 sysroot 关键文件:"
for f in \
    "$SYSROOT/usr/lib/aarch64-linux-gnu/libc.so.6" \
    "$SYSROOT/usr/lib/aarch64-linux-gnu/libstdc++.so.6" \
    "$SYSROOT/usr/lib/aarch64-linux-gnu/libQt5Core.so.5" ; do
    if [[ -e "$f" ]]; then echo "   OK  $f"; else echo "   缺失  $f"; fi
done

# sysroot 里 libc 的最高 GLIBC 符号 = 板子实际 glibc 版本
LIBC=$(find "$SYSROOT" -name libc.so.6 2>/dev/null | head -1)
if [[ -n "$LIBC" ]]; then
    echo ">> 板子 glibc 实际版本 (最高符号):"
    strings "$LIBC" | grep -oE '^GLIBC_[0-9.]+' | sort -V | uniq | tail -3
fi

echo
echo ">> 完成。接下来构建 (方案一):"
echo "   ./scripts/build_rk3568.sh --sysroot $SYSROOT"
