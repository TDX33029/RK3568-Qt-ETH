#!/bin/bash
# 在 RK3568（Debian/Ubuntu aarch64）上本机编译并运行
set -e

echo "== 安装依赖（首次）=="
sudo apt-get update
sudo apt-get install -y g++ cmake

echo "== 编译 =="
cmake -B build
cmake --build build -j

echo "== 启用 spidev（RK3568 需在设备树启用对应 spi 节点）=="
echo "   默认设备 /dev/spidev2.0 需存在，否则用 --dev 指定"
ls -l /dev/spidev* 2>/dev/null || echo "  未找到 spidev，请检查设备树/DTB"

echo "== 运行（需要 SPI 访问权限，一般加入 spi 组或用 sudo）=="
sudo ./build/spi_master --dev /dev/spidev2.0 --hz 1000000
