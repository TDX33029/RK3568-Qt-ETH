#!/usr/bin/env bash
set -euo pipefail

echo "== Basic System =="
uname -a || true
echo

echo "== CPU Info =="
grep -E 'model name|Processor|Hardware|Revision' /proc/cpuinfo || cat /proc/cpuinfo || true
echo

echo "== Memory =="
free -h || true
echo

echo "== Framebuffer Device =="
if [ -e /dev/fb0 ]; then
  ls -l /dev/fb0
else
  echo "/dev/fb0 not found"
fi
echo

echo "== Input Devices =="
ls -l /dev/input 2>/dev/null || echo "/dev/input not found"
echo

echo "== Mounted Filesystems =="
mount | head -n 20 || true
echo

echo "== GCC =="
gcc --version | head -n 1 || echo "gcc not found"
echo

echo "== Framebuffer Sysfs =="
if [ -d /sys/class/graphics/fb0 ]; then
  for f in name modes mode virtual_size bits_per_pixel; do
    if [ -e "/sys/class/graphics/fb0/$f" ]; then
      printf "%s: " "$f"
      cat "/sys/class/graphics/fb0/$f" || true
    fi
  done
else
  echo "/sys/class/graphics/fb0 not found"
fi
echo

echo "== Probe Done =="
