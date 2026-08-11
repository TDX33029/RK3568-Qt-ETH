#!/usr/bin/env python3
"""
send_eth.py - 通过以太网内网(UDP)把模拟测量帧发给板子(RK3568 / ETH1)

帧格式与 RK3568 端 EthReader 一致: 每个 UDP 数据报 = 一整帧 182 字节 SpiFrame
(见 protocol.py: SpiFrame / DemoSim.step)。板子端用 recv_probe.py 检测是否收到。

用法:
    # 默认发给 192.168.1.10:5000, 100Hz
    python send_eth.py

    # 指定板子 ETH1 地址/端口、速率、场景
    python send_eth.py --target 192.168.1.20 --port 5000 --rate 100 --scene 0

依赖: 仅标准库 + protocol.py (同目录)
"""
from __future__ import annotations
import argparse
import socket
import sys
import time

from protocol import (
    DemoSim, Config,
    SCENE_STRAIGHT, SCENE_CLIMB, SCENE_TURN, SCENE_CUSTOM,
    SPI_FRAME_LEN, SPI_MAGIC0, SPI_MAGIC1,
)


def main() -> int:
    ap = argparse.ArgumentParser(description="以太网(UDP)发送模拟测量帧到板子")
    ap.add_argument("--target", default="192.168.1.10",
                    help="板子 ETH1 接收端 IP (默认 192.168.1.10)")
    ap.add_argument("--port", type=int, default=5000,
                    help="板子 UDP 监听端口 (默认 5000)")
    ap.add_argument("--rate", type=int, default=100,
                    help="发送帧率 Hz (默认 100)")
    ap.add_argument("--scene", type=int, default=SCENE_STRAIGHT,
                    choices=[SCENE_STRAIGHT, SCENE_CLIMB, SCENE_TURN, SCENE_CUSTOM],
                    help="运动场景 0=直线 1=爬升 2=转弯 3=自定义")
    ap.add_argument("--duration", type=float, default=0.0,
                    help="发送时长(秒); 0=一直发 (默认 0)")
    ap.add_argument("--src-port", type=int, default=0,
                    help="本机发送源端口 (默认 0=系统分配)")
    args = ap.parse_args()

    cfg = Config(scene=args.scene, sample_rate_hz=args.rate)
    sim = DemoSim(cfg)

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    if args.src_port:
        sock.bind(("", args.src_port))

    dest = (args.target, args.port)
    period = 1.0 / args.rate if args.rate > 0 else 0.0
    seq = 0
    sent = 0
    t0 = time.time()
    t_next = t0

    print(f"[send_eth] -> {dest}  rate={args.rate}Hz  scene={args.scene}  "
          f"frame_len={SPI_FRAME_LEN}B  (Ctrl+C 停止)")
    try:
        while True:
            now_us = int((time.time() - t0) * 1e6)
            frame, _truth = sim.step(now_us)
            sock.sendto(frame, dest)
            seq = (seq + 1) & 0xFFFF
            sent += 1

            if sent % args.rate == 0:
                el = time.time() - t0
                print(f"  sent={sent}  t={el:.1f}s  ~{sent / el:.1f} fps  "
                      f"last_seq={seq}")

            if args.duration and (time.time() - t0) >= args.duration:
                break

            t_next += period
            sleep_for = t_next - time.time()
            if sleep_for > 0:
                time.sleep(sleep_for)
            elif sleep_for < -0.05:
                t_next = time.time()   # 落后太多则重新对齐, 不盲目追帧
    except KeyboardInterrupt:
        print("\n[send_eth] 用户中断")
    finally:
        sock.close()

    el = time.time() - t0
    print(f"[send_eth] 完成: 共发 {sent} 帧, 平均 {sent / el:.1f} fps")
    return 0


if __name__ == "__main__":
    sys.exit(main())
