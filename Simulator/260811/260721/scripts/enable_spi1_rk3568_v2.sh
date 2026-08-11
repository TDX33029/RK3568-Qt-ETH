#!/usr/bin/env bash
# =============================================================================
#  v2: 启用 SPI1, 处理 v1 漏掉的 142320 字节板级 dtb(被加载的那份)。
#  那份 dtb 后面紧接 kernel, 不能长大。办法:
#    反编译 -> 删 __symbols__(省~25KB)-> 重编译 -> 加 spidev 子节点(~100B)
#    新 dtb < 原 dtb, 只覆盖原 dtb 自己的区域, 不碰后面的 kernel。
#  v1 已改(有 spidev)的自动跳过。__symbols__ 仅运行时 overlay 用, 本板未用,
#  删掉不影响启动(U-Boot 不靠它做 fixup)。
#  用法: sudo bash scripts/enable_spi1_rk3568_v2.sh
# =============================================================================
set -euo pipefail
[ "$(id -u)" -eq 0 ] || { echo "需要 root"; exit 1; }

BAK=/userdata/spi1_backup_v2_$(date +%Y%m%d_%H%M%S)
mkdir -p "$BAK"
echo "备份目录: $BAK"; echo

echo ">> 扫描板级 dtb (>30KB)..."
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
        if 30000 < ts <= len(data)-i: print(f"{dev} {i} {ts}")
        i+=4
PY
)
[ "${#FOUND[@]}" -gt 0 ] || { echo "未找到板级 dtb"; exit 1; }
printf '   %s\n' "${FOUND[@]}"; echo

CHANGED=0
declare -A BK_DONE
for line in "${FOUND[@]}"; do
  set -- $line; DEV=$1; OFF=$2; SIZE=$3
  PARTNAME=$(basename "$DEV")
  echo "=== $DEV  offset=$OFF  size=$SIZE ==="

  # 备份整分区(每分区只备一次)
  if [ -z "${BK_DONE[$PARTNAME]:-}" ]; then
    dd if="$DEV" of="$BAK/${PARTNAME}.img" bs=1M status=none
    BK_DONE[$PARTNAME]=1
  fi

  # 提取原 dtb
  dd if="$DEV" of=/tmp/orig.dtb bs=1 skip="$OFF" count="$SIZE" status=none

  # 幂等: 已有 spidev(v1 改过)就跳过
  if fdtget /tmp/orig.dtb /spi@fe620000/spidev@0 compatible >/dev/null 2>&1; then
    echo "   已有 spidev(v1 处理过), 跳过"; echo; continue
  fi

  # 反编译 -> 删 __symbols__ -> 重编译
  dtc -I dtb -O dts /tmp/orig.dtb -o /tmp/orig.dts 2>/dev/null
  python3 - /tmp/orig.dts /tmp/strip.dts <<'PY'
import sys
s=open(sys.argv[1]).read()
i=s.find('__symbols__')
if i>=0:
    j=s.find('{', i); depth=0; k=j
    while k<len(s):
        if s[k]=='{': depth+=1
        elif s[k]=='}':
            depth-=1
            if depth==0:
                end=k+1
                if end<len(s) and s[end]==';': end+=1
                ls=s.rfind('\n',0,i)+1
                s=s[:ls]+s[end:].lstrip('\n')
                break
        k+=1
open(sys.argv[2],'w').write(s)
PY
  dtc -I dts -O dtb -o /tmp/work.dtb /tmp/strip.dts 2>/dev/null

  # 加 spi1 status=okay + spidev@0 子节点
  fdtput -t s /tmp/work.dtb /spi@fe620000 status okay
  fdtput -c  /tmp/work.dtb /spi@fe620000/spidev@0
  fdtput -t s /tmp/work.dtb /spi@fe620000/spidev@0 compatible rochester,spidev
  fdtput -t x /tmp/work.dtb /spi@fe620000/spidev@0 reg 0x0
  fdtput -t x /tmp/work.dtb /spi@fe620000/spidev@0 spi-max-frequency 0x2faf080

  # 校验合法
  dtc -I dtb -O dts /tmp/work.dtb -o /dev/null 2>/dev/null || { echo "   新 dtb 非法, 跳过"; echo; continue; }

  NEWSIZE=$(stat -c%s /tmp/work.dtb)
  echo "   原 size=$SIZE  新 size=$NEWSIZE (删 symbols 后)"
  if [ "$NEWSIZE" -gt "$SIZE" ]; then
    echo "   仍大于原 size(symbols 不够大?), 跳过避免覆盖后续"; echo; continue
  fi

  # 写回原位置(只覆盖 [OFF, OFF+NEWSIZE] ⊆ [OFF, OFF+SIZE], 不碰后面)
  dd if=/tmp/work.dtb of="$DEV" bs=1 seek="$OFF" conv=notrunc status=none
  CHANGED=$((CHANGED+1))
  echo "   写回完成"; echo
done

echo "========================================"
echo " 本次改动: $CHANGED"
echo " 备份目录: $BAK"
echo "========================================"
echo "下一步: sudo reboot"
echo "重启后: ls -l /dev/spidev1.0"
echo
echo "回滚: dd if=$BAK/mmcblk0p3.img of=/dev/mmcblk0p3 bs=1M  (p4 同理)"
