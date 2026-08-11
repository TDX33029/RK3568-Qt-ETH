#!/usr/bin/env bash
# =============================================================================
#  板上启动 ps_tracker_ui (避开显示挂死):
#    - 停掉 lightdm 桌面 (它在反复试 HDMI atomic modeset, 会卡死显示)
#    - 用 linuxfb 平台插件, 直接写 /dev/fb0 -> HDMI, 完全绕开 DRM atomic
#    - --auto-live: 启动即收 SPI 数据 (无需点按钮; 无显示时串口看 stdout)
#
#  用法:
#    sudo bash scripts/run_on_board.sh            # linuxfb 显示 + 自动收数据
#    sudo bash scripts/run_on_board.sh offscreen   # 不显示, 仅串口看数据 (验证用)
#    sudo bash scripts/run_on_board.sh gui         # linuxfb 但不 auto-live (手动点 Start)
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BIN="$SRC_DIR/build/ps_tracker_ui"
[ -x "$BIN" ] || BIN="$(find / -name ps_tracker_ui -type f -executable 2>/dev/null | head -1)"
[ -x "$BIN" ] || { echo "找不到 ps_tracker_ui, 先构建"; exit 1; }
echo "程序: $BIN"

# 停桌面 (lightdm 在 atomic 循环里, 不停会抢显示/卡死)
if command -v systemctl >/dev/null && systemctl is-active lightdm >/dev/null 2>&1; then
    echo "停 lightdm 桌面..."
    systemctl stop lightdm || true
    sleep 1
fi

# 选平台
MODE="${1:-fb}"
case "$MODE" in
    offscreen) export QT_QPA_PLATFORM=offscreen; AUTO="--auto-live" ;;
    gui)      export QT_QPA_PLATFORM=linuxfb;  AUTO="" ;;
    fb|*)     export QT_QPA_PLATFORM=linuxfb;  AUTO="--auto-live" ;;
esac
echo "平台: $QT_QPA_PLATFORM  auto-live: ${AUTO:-否}"

cd "$SRC_DIR"
exec "$BIN" $AUTO "$@"
