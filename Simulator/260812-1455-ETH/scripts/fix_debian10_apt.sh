#!/usr/bin/env bash
# =============================================================================
#  修复 Debian 10 (buster, 已 EOL) 的 apt 源:
#    - 主源 deb.debian.org 已撤掉 buster, apt-get update 会 404
#    - buster Release 已过期, apt 会拒绝 "Release file expired"
#  本脚本:
#    1) 备份 /etc/apt/sources.list
#    2) 切到 Debian 官方归档源 (archive.debian.org; buster EOL 后主源已撤)
#    3) 关闭 Release 过期校验 (Acquire::Check-Valid-Until=false)
#    4) apt-get update
#
#  用法:  sudo bash scripts/fix_debian10_apt.sh
#  跑完即可 apt install / 跑 init_board.sh。
# =============================================================================
set -euo pipefail
[ "$(id -u)" -eq 0 ] || { echo "需要 root, 请用 sudo 运行"; exit 1; }

echo ">> [1/4] 备份原 sources.list"
ts=$(date +%Y%m%d_%H%M%S)
cp -a /etc/apt/sources.list "/etc/apt/sources.list.bak.$ts" 2>/dev/null && echo "   已备份 -> /etc/apt/sources.list.bak.$ts" || echo "   (无原文件可备份)"

echo ">> [2/4] 切到 Debian 官方归档源 (archive.debian.org)"
cat > /etc/apt/sources.list <<'EOF'
deb http://archive.debian.org/debian buster main
deb http://archive.debian.org/debian-security buster/updates main
EOF
echo "   /etc/apt/sources.list 已更新"

echo ">> [3/4] 关闭 Release 过期校验"
mkdir -p /etc/apt/apt.conf.d
cat > /etc/apt/apt.conf.d/99no-valid-until <<'EOF'
Acquire::Check-Valid-Until "false";
EOF
echo "   /etc/apt/apt.conf.d/99no-valid-until 已写入"

echo ">> [4/4] apt-get update"
apt-get update

echo
echo ">> 完成。现在可以:"
echo "   sudo apt install <包>            # 直接装包"
echo "   bash scripts/init_board.sh       # 或跑初始化脚本(已集成此修复)"
echo
echo "   回滚: sudo cp /etc/apt/sources.list.bak.$ts /etc/apt/sources.list"
