#!/usr/bin/env python3
"""
recv_probe.py - 板子(RK3568)端 UDP 接收探针, 检测是否收到 Host 发来的测量帧

在板子上运行, 绑定 ETH1 端口收 UDP 数据报(每包=182 字节 SpiFrame),
用与 spi_protocol.h 一致的 魔数 + CRC16/CCITT-FALSE 校验, 统计:
    - 收到帧数 / CRC 错误数 / 长度错误数
    - 最新 seq 及是否连续(跳变即丢包)
    - 每 N 秒打印一次状态

配合 Host 端 send_eth.py 使用, 用于验证 ETH1 内网链路是否打通。

用法:
    python recv_probe.py                 # 默认 0.0.0.0:5000
    python recv_probe.py --port 5000 --bind 192.168.1.10
    python recv_probe.py --verbose       # 每收到一帧打印 seq/dt

依赖: 仅标准库
"""
from __future__ import annotations
import argparse
import socket
import struct
import sys
import time

SPI_MAGIC0 = 0xA5
SPI_MAGIC1 = 0x5A
SPI_FRAME_LEN = 182
SPI_MAX_ANCHORS = 8
ANCHOR_FMT = "<Bfffff"          # 21B, 与 protocol.py 一致
ANCHOR_SIZE = struct.calcsize(ANCHOR_FMT)
SPI_HDR_FMT = "<HBBIH"          # seq(2)+mode_mask(1)+n_anc(1)+dt_us(4)+reserved(2)


def crc16(data: bytes) -> int:
    """CRC-16/CCITT-FALSE, 与 spi_protocol.h / protocol.py 一致"""
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def validate(buf: bytes):
    """返回 (ok, info_dict)。校验 长度/魔数/CRC。"""
    if len(buf) != SPI_FRAME_LEN:
        return False, {"err": "len", "got": len(buf)}
    if buf[0] != SPI_MAGIC0 or buf[1] != SPI_MAGIC1:
        return False, {"err": "magic"}
    crc_recv = buf[180] | (buf[181] << 8)
    crc_calc = crc16(buf[:180])
    if crc_recv != crc_calc:
        return False, {"err": "crc", "recv": crc_recv, "calc": crc_calc}
    seq, mode_mask, n_anc, dt_us, _res = struct.unpack(SPI_HDR_FMT, buf[2:12])
    return True, {"seq": seq, "mode_mask": mode_mask, "n_anc": n_anc, "dt_us": dt_us}


def main() -> int:
    ap = argparse.ArgumentParser(description="板子 UDP 接收探针(检测 ETH1 是否收到帧)")
    ap.add_argument("--bind", default="0.0.0.0",
                    help="绑定地址 (默认 0.0.0.0 监听所有接口; 仅收 ETH1 填其 IP)")
    ap.add_argument("--port", type=int, default=5000, help="监听端口 (默认 5000)")
    ap.add_argument("--verbose", action="store_true", help="每帧打印 seq/dt")
    ap.add_argument("--report", type=float, default=2.0,
                    help="状态打印间隔(秒) (默认 2.0)")
    args = ap.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind((args.bind, args.port))
    sock.settimeout(0.2)

    print(f"[recv_probe] listening {args.bind}:{args.port}  (Ctrl+C 停止)")
    print(f"            等待 Host 端 send_eth.py 发送 SpiFrame({SPI_FRAME_LEN}B)...")

    n_ok = 0
    n_crc = 0
    n_len = 0
    n_magic = 0
    last_seq = None
    drops = 0
    t0 = time.time()
    t_report = t0

    try:
        while True:
            try:
                buf, addr = sock.recvfrom(4096)
            except socket.timeout:
                buf = None
            if buf is None:
                pass
            else:
                ok, info = validate(buf)
                if ok:
                    n_ok += 1
                    if last_seq is not None:
                        exp = (last_seq + 1) & 0xFFFF
                        if info["seq"] != exp:
                            drops += (info["seq"] - exp) & 0xFFFF
                    last_seq = info["seq"]
                    if args.verbose:
                        print(f"  frame seq={info['seq']} dt={info['dt_us']/1e6:.4f}s "
                              f"n_anc={info['n_anc']} from={addr[0]}")
                else:
                    if info.get("err") == "crc":
                        n_crc += 1
                        if args.verbose:
                            print(f"  CRC ERROR recv={info['recv']:#x} calc={info['calc']:#x}")
                    elif info.get("err") == "magic":
                        n_magic += 1
                    else:
                        n_len += 1
                        if args.verbose:
                            print(f"  LEN ERROR got={info.get('got')}")

            now = time.time()
            if now - t_report >= args.report:
                el = now - t0
                fps = n_ok / el if el > 0 else 0
                print(f"[recv_probe] ok={n_ok} ({fps:.1f}fps) crc_err={n_crc} "
                      f"magic_err={n_magic} len_err={n_len} drops={drops} "
                      f"last_seq={last_seq}")
                t_report = now
    except KeyboardInterrupt:
        print("\n[recv_probe] 用户中断")
    finally:
        sock.close()

    el = time.time() - t0
    print(f"[recv_probe] 总计: ok={n_ok} crc_err={n_crc} magic_err={n_magic} "
          f"len_err={n_len} drops={drops}  ({el:.1f}s)")
    # 判定: 收到任意有效帧即认为链路通
    return 0 if n_ok > 0 else 1


if __name__ == "__main__":
    sys.exit(main())
