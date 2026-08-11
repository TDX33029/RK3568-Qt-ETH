#!/usr/bin/env bash
# =============================================================================
#  RK3568 启用 SPI1 + spidev, 生成 /dev/spidev1.0
#  做法: 扫 eMMC 分区找板级 dtb(>30KB) -> 备份整分区 -> fdtput 改
#        /spi@fe620000 status=okay + 加 spidev@0 子节点 -> 写回原位置
#  安全: 每个改动分区全盘备份 + 改动后填充校验(只覆盖原 dtb 区域或其后的 0 填充)
#  回滚: dd 恢复分区备份
#
#  用法:  sudo bash scripts/enable_spi1_rk3568.sh
# =============================================================================
set -euo pipefail
[ "$(id -u)" -eq 0 ] || { echo "需要 root, 请用 sudo 运行"; exit 1; }

BAK=/userdata/spi1_backup_$(date +%Y%m%d_%H%M%S)
mkdir -p "$BAK"
echo "备份目录: $BAK"
echo

# ---- 1. 扫 p1/p3/p4/p5 找板级 dtb (>30KB, 排除小的 uboot/resource dtb) ----
echo ">> 扫描板级 dtb..."
mapfile -t FOUND < <(python3 - <<'PY'
import struct
for p in ['p1','p3','p4','p5']:
    dev=f'/dev/mmcblk0{p}'
    try:
        with open(dev,'rb') as f: data=f.read()
    except Exception: continue
    i=0
    while True:
        i=data.find(b'\xd0\x0d\xfe\xed', i)
        if i<0: break
        try: ts=struct.unpack('>I', data[i+4:i+8])[0]
        except: ts=0
        if 30000 < ts <= len(data)-i:        # 板级 dtb 通常 >30KB
            print(f"{dev} {i} {ts}")
        i+=4
PY
)
if [ "${#FOUND[@]}" -eq 0 ]; then
  echo "!! 未在 p1/p3/p4/p5 找到板级 dtb(可能附在 kernel Image 里)。"
  echo "   这种情况需用 ALIENTEK SDK 重编 dtb, 或把改好的 dtb 放到 U-Boot 加载处。"
  exit 1
fi
echo "找到板级 dtb:"
printf '   %s\n' "${FOUND[@]}"
echo

# ---- 2. 逐个备份 + 改 + 写回 ----
CHANGED=0
for line in "${FOUND[@]}"; do
  set -- $line; DEV=$1; OFF=$2; SIZE=$3
  PARTNAME=$(basename "$DEV")
  echo "=== $DEV  offset=$OFF  size=$SIZE ==="

  # 备份整分区
  echo "   备份分区 -> $BAK/${PARTNAME}.img"
  dd if="$DEV" of="$BAK/${PARTNAME}.img" bs=1M status=none

  # 提取原 dtb
  dd if="$DEV" of="/tmp/orig.dtb" bs=1 skip="$OFF" count="$SIZE" status=none
  cp /tmp/orig.dtb /tmp/new.dtb

  # 改: spi1 status=okay + 加 spidev@0 子节点
  fdtput -t s /tmp/new.dtb /spi@fe620000 status okay
  fdtput -c  /tmp/new.dtb /spi@fe620000/spidev@0
  fdtput -t s /tmp/new.dtb /spi@fe620000/spidev@0 compatible rochester,spidev
  fdtput -t x /tmp/new.dtb /spi@fe620000/spidev@0 reg 0x0
  fdtput -t x /tmp/new.dtb /spi@fe620000/spidev@0 spi-max-frequency 0x2faf080

  # 校验新 dtb 合法
  dtc -I dtb -O dts /tmp/new.dtb -o /dev/null 2>/dev/null || { echo "   新 dtb 校验失败, 跳过"; continue; }

  NEWSIZE=$(stat -c%s /tmp/new.dtb)
  echo "   原 size=$SIZE  新 size=$NEWSIZE"

  # 若变大, 校验原 dtb 之后那段是 0 填充(避免覆盖后续数据)
  if [ "$NEWSIZE" -gt "$SIZE" ]; then
    GROW=$((NEWSIZE-SIZE))
    PAD_OK=$(python3 - "$DEV" "$OFF" "$SIZE" "$GROW" <<'PY'
import sys
dev,off,size,grow=sys.argv[1],int(sys.argv[2]),int(sys.argv[3]),int(sys.argv[4])
with open(dev,'rb') as f:
    f.seek(off+size); chunk=f.read(grow)
print("ok" if all(b==0 for b in chunk) else "no")
PY
)
    if [ "$PAD_OK" != "ok" ]; then
      echo "   !! 原 dtb 之后非 0 填充, 写回可能覆盖后续数据, 跳过 $DEV"
      continue
    fi
  fi

  # 写回原位置
  echo "   写回 $DEV @ offset $OFF"
  dd if=/tmp/new.dtb of="$DEV" bs=1 seek="$OFF" conv=notrunc status=none
  CHANGED=$((CHANGED+1))
  echo "   $DEV 完成"
  echo
done

if [ "$CHANGED" -eq 0 ]; then
  echo "!! 没有任何分区被改动(全是非安全情况)。备份在 $BAK, 未改动不影响系统。"
  exit 1
fi

echo "========================================"
echo " 改动分区数: $CHANGED"
echo " 备份目录  : $BAK"
echo "========================================"
echo "下一步: sudo reboot"
echo "重启后验证: ls -l /dev/spidev1.0   (应出现)"
echo
echo "回滚(若起不来或异常, 用读卡器在别的机器上恢复对应分区):"
ls -1 "$BAK"/*.img | sed 's/^/   dd if=/; s/$/ of=\/dev\/mmcblk0pX bs=1M/'
echo
echo "或在本机(若还能启动到这):"
echo "   for f in $BAK/*.img; do dd if=\$f of=\${f##*/对应的分区}; done"
echo "   (按文件名 pX 对应 /dev/mmcblk0pX)"
