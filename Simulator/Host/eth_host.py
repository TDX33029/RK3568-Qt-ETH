#!/usr/bin/env python3
"""
eth_host.py - ETH1/UDP 上位机 (RK3568 定位接收端配套, 取代旧 UART-F407 上位机)

数据链路 (本项目不使用 SPI):
    PC 上位机 --UDP--> RK3568 ETH1:5000 (UDP 服务端, 回 PONG 应答)

本程序用与板端 EKF 完全一致的模型 (protocol.py::DemoSim) 生成测量帧:
每个 UDP 数据报 = 一整帧 182 字节 UdpFrame (magic/seq/mode_mask/n_anc/dt_us/
anchors[8]/crc16, 与 260721/frame_protocol.h 逐字节一致), 发往板子 ETH1 端口。
板子端校验魔数+CRC 后喂给 EKF 实时定位显示。

联通测试 (双向):
  - ICMP ping: 验证网络层可达 (子进程 ping 命令)
  - UDP PING/PONG: 发 8B 探测帧, 板端 (260721 eth_reader/ethtest) 回 PONG,
    上位机据此确认板端 5000 端口接收链路双向可用并测 RTT。发送中每 2s
    自动心跳探测一次, 链路状态灯实时反映"板端是否真的在收"。

界面 (简版):
  - 目标: 板子 ETH1 IP + 端口 (默认 192.168.1.10:5000) + 联通测试按钮
  - 配置: 场景 / 采样率 / 模态 (TDOA/TOA/AOA/RSS) / 自定义初值(仅 Custom 场景)
  - 控制: 启动发送 / 停止 / 清空
  - 位置图: 真值轨迹 + 锚点 (本地参照, 与板端 EKF 估计对照)
  - 状态: 链路状态灯 / 已发帧数 / 实际发送速率 / 最新 seq / 发送错误 /
          板卡应答 PONG 数 / 最近 RTT

依赖: PyQt5 + pyqtgraph (旧 host_gui.py 的 pyserial 已不再需要)
运行: python eth_host.py
"""
from __future__ import annotations
import platform
import re
import socket
import subprocess
import sys
import time
from collections import deque

from PyQt5 import QtCore, QtWidgets
import pyqtgraph as pg

import protocol as P

DEFAULT_TARGET_IP = "192.168.1.10"
DEFAULT_PORT = 5000
POS_TRAIL = 400
ANCHOR_COLOR = "#e74c3c"
TRAIL_COLOR = "#2980b9"
HEARTBEAT_MS = 2000        # 发送中自动心跳探测周期
PING_TIMEOUT_MS = 1500     # 联通测试等待应答超时


def icmp_ping(ip: str, timeout_ms: int = 1500):
    """网络层可达性检测 (ICMP, 子进程 ping)。返回 (rtt_ms 或 None, 描述)。

    注意: 判定以 ping 命令 returncode 为准 (中英文系统均可靠);
    输出文本仅用于解析 RTT (中文系统是 "时间=1ms", 英文是 "time=1ms")。
    """
    if platform.system() == "Windows":
        # -n 2: 首次包常做 ARP 解析而超时, 第二次有缓存更可靠
        cmd = ["ping", "-n", "2", "-w", str(timeout_ms), ip]
        kwargs = {"creationflags": 0x08000000}      # CREATE_NO_WINDOW, 不弹黑窗
    else:
        cmd = ["ping", "-c", "1", "-W", str(max(1, timeout_ms // 1000)), ip]
        kwargs = {}
    try:
        out = subprocess.run(cmd, capture_output=True, text=True,
                             timeout=timeout_ms / 1000 + 3, **kwargs)
    except (subprocess.TimeoutExpired, OSError) as e:
        return None, f"ping 命令失败: {e}"
    text = (out.stdout or "") + (out.stderr or "")
    if out.returncode != 0:
        return None, "网络不可达 (ICMP 无响应, 检查网线/IP/防火墙)"
    m = re.search(r"(?:时间|time)\s*[=<]\s*([\d.]+)\s*ms", text, re.I)
    rtt = float(m.group(1)) if m else None
    if rtt is not None:
        return rtt, f"网络可达 (RTT {rtt:.1f} ms)"
    return None, "网络可达"


class Sender(QtCore.QObject):
    """UDP 帧发送器: 1ms 轮询节流发帧 + 心跳探测 + PONG 应答接收。"""
    error = QtCore.pyqtSignal(str)
    sent = QtCore.pyqtSignal(int)          # 每发一帧: 新 seq
    pong = QtCore.pyqtSignal(int, float)   # 收到板卡应答: seq, rtt_ms

    def __init__(self, parent=None):
        super().__init__(parent)
        self.cfg = P.Config()
        self.sim = P.DemoSim(self.cfg)
        self.sock = None
        self.dest = (DEFAULT_TARGET_IP, DEFAULT_PORT)
        self._ensure_sock()          # 立即建 socket: 联通测试不依赖"启动发送"
        self._t0 = 0.0
        self._timer = QtCore.QTimer(self)          # 1ms 节流发帧
        self._timer.timeout.connect(self._tick)
        self._recv = QtCore.QTimer(self)           # 50ms 轮询收 PONG (常开:
        self._recv.setInterval(50)                 # 联通测试不依赖"启动发送")
        self._recv.timeout.connect(self._recv_poll)
        self._recv.start()
        self._heartbeat = QtCore.QTimer(self)      # 发送中自动心跳探测
        self._heartbeat.setInterval(HEARTBEAT_MS)
        self._heartbeat.timeout.connect(self.send_ping)

        self._ping_seq = 0
        self._ping_t0 = 0.0
        self._pings_out = 0
        self._pongs_in = 0
        self._last_pong_at = 0.0                   # 最近应答时刻 (0=从未)
        self._last_rtt_ms = 0.0

    def set_dest(self, ip: str, port: int):
        self.dest = (ip, port)

    def apply_config(self, cfg: P.Config):
        self.cfg = cfg
        self.sim.apply(cfg)

    def _ensure_sock(self):
        """确保 socket 存在且绑定本机源端口 (PONG 回送到此端口才能收到)。"""
        if self.sock is not None:
            return True
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            s.setblocking(False)
            s.bind(("", 0))
            self.sock = s
            return True
        except OSError as e:
            self.error.emit(f"创建 UDP 套接字失败: {e}")
            return False

    def start(self):
        if not self._ensure_sock():
            return False
        self.sim.reset()
        self._t0 = time.monotonic()
        self._period_us = 1e6 / max(self.cfg.sample_rate_hz, 1)
        self._next_us = 0.0
        # 恒定 1ms 轮询 + 单调时钟节流: 任何采样率都按真实时间精确发帧。
        # (Windows 上 QTimer 仅对 <16ms 间隔提升系统定时器精度, 直接按
        #  速率设间隔在 20~200ms 档会被 15.6ms tick 量化, 导致速率漂移)
        self._timer.start(1)
        self.send_ping()                           # 启动立即探测一次
        self._heartbeat.start()
        return True

    def stop(self):
        self._timer.stop()
        self._heartbeat.stop()
        # _recv 保持常开: 停止发送后联通测试仍可收 PONG

    def close(self):
        self.stop()
        self._recv.stop()
        s = self.sock
        self.sock = None
        if s:
            try: s.close()
            except OSError: pass

    def send_ping(self):
        """发 8B PING 探测帧 (按钮与心跳共用)。"""
        if not self._ensure_sock():
            return
        seq = self._ping_seq
        self._ping_seq = (self._ping_seq + 1) & 0xFF
        pkt = bytes((0xA5, 0x5A, ord('P'), ord('I'), ord('N'), ord('G'), seq, 0))
        self._ping_t0 = time.monotonic()
        try:
            self.sock.sendto(pkt, self.dest)
            self._pings_out += 1
        except OSError as e:
            self.error.emit(f"PING 发送失败: {e}")

    def _recv_poll(self):
        while True:
            try:
                data, _ = self.sock.recvfrom(2048)
            except (BlockingIOError, OSError):
                break
            if (len(data) == 8 and data[0] == 0xA5 and data[1] == 0x5A
                    and data[2:6] == b"PONG"):
                self._pongs_in += 1
                self._last_pong_at = time.monotonic()
                self._last_rtt_ms = (time.monotonic() - self._ping_t0) * 1000.0
                self.pong.emit(data[6], self._last_rtt_ms)

    def _tick(self):
        now_us = int((time.monotonic() - self._t0) * 1e6)
        if now_us < self._next_us:
            return                       # 未到发帧时刻
        self._next_us += self._period_us
        if now_us - self._next_us > 1e6:
            self._next_us = now_us       # 落后超过 1s, 重新对齐
        frame, _truth = self.sim.step(now_us)   # dt 由 now_us 差分自算, 精确
        try:
            self.sock.sendto(frame, self.dest)
        except OSError as e:
            self.error.emit(f"sendto {self.dest[0]}:{self.dest[1]} 失败: {e}")
            self.stop()
            return
        self.sent.emit(self.sim._seq - 1)


class MainWindow(QtWidgets.QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("ETH/UDP 上位机 - RK3568 定位接收端")
        self.resize(1080, 680)

        self.cfg = P.Config()
        self.sender = Sender(self)
        self.sender.error.connect(self._on_error)
        self.sender.sent.connect(self._on_sent)
        self.sender.pong.connect(self._on_pong)

        self._sent = 0
        self._errors = 0
        self._last_seq = 0
        self._fps = 0.0
        self._fps_t0 = time.monotonic()
        self._fps_cnt = 0
        self._ping_btn_pending = False   # 联通测试按钮等待应答中
        self._udp_rtt = None             # 最近一次按钮测得的 UDP RTT

        # 真值轨迹 (本地参照)
        self._tx = deque(maxlen=POS_TRAIL)
        self._ty = deque(maxlen=POS_TRAIL)
        self._cur_xy = (0.0, 0.0)

        self._build_ui()
        self._sync_cfg_to_widgets()

        self._ui_timer = QtCore.QTimer(self)
        self._ui_timer.timeout.connect(self._refresh_status)
        self._ui_timer.start(250)

    # ---------------- UI ----------------
    def _build_ui(self):
        central = QtWidgets.QWidget()
        self.setCentralWidget(central)
        root = QtWidgets.QHBoxLayout(central)

        left = QtWidgets.QFrame()
        left.setFrameShape(QtWidgets.QFrame.StyledPanel)
        left.setMinimumWidth(300)
        left.setMaximumWidth(340)
        ll = QtWidgets.QVBoxLayout(left)
        root.addWidget(left)
        root.addWidget(self._build_plot(), 1)

        # 目标连接
        g = QtWidgets.QGroupBox("目标 Target (板子 ETH1)")
        f = QtWidgets.QFormLayout(g)
        self.in_ip = QtWidgets.QLineEdit(DEFAULT_TARGET_IP)
        self.in_port = QtWidgets.QSpinBox()
        self.in_port.setRange(1, 65535)
        self.in_port.setValue(DEFAULT_PORT)
        f.addRow("板子 IP", self.in_ip)
        f.addRow("端口", self.in_port)
        ping_row = QtWidgets.QHBoxLayout()
        self.ping_btn = QtWidgets.QPushButton("🔍 联通测试")
        self.ping_btn.clicked.connect(self._cmd_ping)
        self.lbl_ping = QtWidgets.QLabel("未测试")
        self.lbl_ping.setWordWrap(True)
        ping_row.addWidget(self.ping_btn)
        ping_row.addWidget(self.lbl_ping, 1)
        f.addRow("", ping_row)
        ll.addWidget(g)

        # 仿真配置
        g = QtWidgets.QGroupBox("仿真配置 Config")
        f = QtWidgets.QFormLayout(g)
        self.in_scene = QtWidgets.QComboBox()
        for k, v in P.SCENE_NAMES.items():
            self.in_scene.addItem(v, k)
        self.in_rate = QtWidgets.QSpinBox()
        self.in_rate.setRange(1, 200)
        self.in_rate.setValue(self.cfg.sample_rate_hz)
        mod = QtWidgets.QHBoxLayout()
        self.chk_tdoa = QtWidgets.QCheckBox("TDOA")
        self.chk_toa = QtWidgets.QCheckBox("TOA")
        self.chk_aoa = QtWidgets.QCheckBox("AOA")
        self.chk_rss = QtWidgets.QCheckBox("RSS")
        for w in (self.chk_tdoa, self.chk_toa, self.chk_aoa, self.chk_rss):
            mod.addWidget(w)
        self.chk_tdoa.setChecked(True)
        self.chk_toa.setChecked(True)
        self.chk_aoa.setChecked(True)
        self.chk_rss.setChecked(True)

        self.in_ix = QtWidgets.QDoubleSpinBox()
        self.in_iy = QtWidgets.QDoubleSpinBox()
        self.in_iz = QtWidgets.QDoubleSpinBox()
        self.in_vx = QtWidgets.QDoubleSpinBox()
        self.in_vy = QtWidgets.QDoubleSpinBox()
        self.in_vz = QtWidgets.QDoubleSpinBox()
        for w in (self.in_ix, self.in_iy, self.in_iz, self.in_vx, self.in_vy, self.in_vz):
            w.setRange(-999.0, 999.0)
            w.setDecimals(2)
            w.setSingleStep(0.5)
        self.in_ix.setValue(self.cfg.init_x)
        self.in_iy.setValue(self.cfg.init_y)
        self.in_iz.setValue(self.cfg.init_z)
        self.in_vx.setValue(self.cfg.vel_x)
        self.in_vy.setValue(self.cfg.vel_y)
        self.in_vz.setValue(self.cfg.vel_z)

        f.addRow("场景 Scene", self.in_scene)
        f.addRow("采样率 Hz", self.in_rate)
        f.addRow("模态", mod)
        f.addRow("初值 X/Y/Z", self._hbox(self.in_ix, self.in_iy, self.in_iz))
        f.addRow("速度 Vx/Vy/Vz", self._hbox(self.in_vx, self.in_vy, self.in_vz))
        ll.addWidget(g)

        # 控制
        g = QtWidgets.QGroupBox("控制 Control")
        gl = QtWidgets.QGridLayout(g)
        self.start_btn = QtWidgets.QPushButton("▶ 启动发送")
        self.stop_btn = QtWidgets.QPushButton("■ 停止")
        self.clear_btn = QtWidgets.QPushButton("🧹 清空")
        self.start_btn.setCheckable(True)
        self.start_btn.toggled.connect(self._toggle_send)
        self.stop_btn.clicked.connect(lambda: self.start_btn.setChecked(False))
        self.clear_btn.clicked.connect(self._cmd_clear)
        gl.addWidget(self.start_btn, 0, 0)
        gl.addWidget(self.stop_btn, 0, 1)
        gl.addWidget(self.clear_btn, 0, 2)
        ll.addWidget(g)

        # 状态
        g = QtWidgets.QGroupBox("状态 Status (发/收统计)")
        f = QtWidgets.QFormLayout(g)
        self.lbl_link = QtWidgets.QLabel("● 未发送")
        self.lbl_link.setStyleSheet("color:#888")
        self.lbl_sent = QtWidgets.QLabel("0")
        self.lbl_fps = QtWidgets.QLabel("-")
        self.lbl_seq = QtWidgets.QLabel("-")
        self.lbl_err = QtWidgets.QLabel("0")
        self.lbl_pong = QtWidgets.QLabel("-")
        self.lbl_rtt = QtWidgets.QLabel("-")
        f.addRow("发送链路", self.lbl_link)
        f.addRow("已发帧数", self.lbl_sent)
        f.addRow("实际速率", self.lbl_fps)
        f.addRow("最新 seq", self.lbl_seq)
        f.addRow("发送错误", self.lbl_err)
        f.addRow("板卡应答 PONG", self.lbl_pong)
        f.addRow("最近 RTT", self.lbl_rtt)
        ll.addWidget(g)

        ll.addStretch(1)

        # 提示
        hint = QtWidgets.QLabel(
            "联调: 板端跑 ./ethtest --verbose\n"
            "或 ./ps_tracker_ui --auto-live\n"
            "板端显示收到即链路通")
        hint.setStyleSheet("color:#888; padding:6px;")
        ll.addWidget(hint)

    @staticmethod
    def _hbox(*widgets):
        h = QtWidgets.QHBoxLayout()
        for w in widgets:
            h.addWidget(w)
        return h

    def _build_plot(self):
        w = QtWidgets.QWidget()
        v = QtWidgets.QVBoxLayout(w)
        pg.setConfigOptions(antialias=True)
        self.plot = pg.PlotWidget(title="真值轨迹 Truth (本地参照) + 锚点 Anchors")
        self.plot.showGrid(x=True, y=True, alpha=0.3)
        self.plot.setLabel("bottom", "X", units="m")
        self.plot.setLabel("left", "Y", units="m")
        self.plot.setAspectLocked(True)
        self.curve_trail = self.plot.plot(pen=pg.mkPen(TRAIL_COLOR, width=2))
        self.target_dot = pg.ScatterPlotItem(
            size=14, symbol="o", brush=pg.mkBrush(TRAIL_COLOR), pen=pg.mkPen("#111", width=1.5))
        self.plot.addItem(self.target_dot)
        self.plot.addItem(pg.ScatterPlotItem(
            pos=[(x, y) for x, y, _z in P.ANCHORS],
            size=[16] * P.N_ANC,
            symbol="t1",
            brush=pg.mkBrush(ANCHOR_COLOR),
            pen=pg.mkPen("#7f0f16", width=1.5)))
        for i, (x, y, _z) in enumerate(P.ANCHORS):
            t = pg.TextItem(f"A{i}", color="#c0392b", anchor=(0.5, -0.6))
            t.setPos(x, y)
            self.plot.addItem(t)
        v.addWidget(self.plot)
        return w

    # ---------------- 发送控制 ----------------
    def _toggle_send(self, on: bool):
        if on:
            self._apply_widgets_to_cfg()
            self.sender.set_dest(self.in_ip.text().strip(), self.in_port.value())
            self.sender.apply_config(self.cfg)
            if not self.sender.start():
                self.start_btn.setChecked(False)
                return
            self._sent = 0
            self._errors = 0
            self._fps_cnt = 0
            self._fps_t0 = time.monotonic()
        else:
            self.sender.stop()
            self.lbl_link.setText("● 已停止")
            self.lbl_link.setStyleSheet("color:#888")

    # ---------------- 联通测试 ----------------
    def _cmd_ping(self):
        """联通测试: ICMP ping (网络层) + UDP PING/PONG (应用层 5000 端口)。"""
        ip = self.in_ip.text().strip()
        self.lbl_ping.setText("检测中… (ICMP + UDP)")
        self.ping_btn.setEnabled(False)
        self._ping_btn_pending = True
        self._udp_rtt = None
        self.sender.send_ping()

        # UDP 应答等待: 1.5s 后取结果
        def finish():
            self.ping_btn.setEnabled(True)
            self._ping_btn_pending = False
            if self._udp_rtt is not None:
                self.lbl_ping.setText(
                    f"✅ 双向通: ICMP 可达, 板端应答 RTT={self._udp_rtt:.1f} ms")
            else:
                self.lbl_ping.setText(
                    f"⚠ {self._icmp_desc} | 板端 5000 端口无 PONG 应答 — "
                    "确认板端已启动接收程序 (ethtest / ps_tracker_ui)")
        QtCore.QTimer.singleShot(PING_TIMEOUT_MS, finish)

        # ICMP 检测 (子进程, 阻塞 ~1s; 放后台线程避免卡 UI)
        self._icmp_desc = "网络不可达"

        def icmp_worker():
            rtt, desc = icmp_ping(ip)
            self._icmp_desc = f"ICMP {rtt:.1f} ms" if rtt is not None else desc
        QtCore.QThreadPool.globalInstance().start(icmp_worker)

    def _on_pong(self, seq: int, rtt_ms: float):
        if self._ping_btn_pending:
            self._udp_rtt = rtt_ms

    def _apply_widgets_to_cfg(self):
        self.cfg.scene = self.in_scene.currentData()
        self.cfg.sample_rate_hz = self.in_rate.value()
        m = 0
        if self.chk_tdoa.isChecked(): m |= P.SPI_MODE_TDOA
        if self.chk_toa.isChecked():  m |= P.SPI_MODE_TOA
        if self.chk_aoa.isChecked():  m |= P.SPI_MODE_AOA
        if self.chk_rss.isChecked():  m |= P.SPI_MODE_RSS
        self.cfg.enable_mask = m if m else (P.SPI_MODE_TDOA | P.SPI_MODE_TOA
                                            | P.SPI_MODE_AOA | P.SPI_MODE_RSS)
        self.cfg.init_x = self.in_ix.value()
        self.cfg.init_y = self.in_iy.value()
        self.cfg.init_z = self.in_iz.value()
        self.cfg.vel_x = self.in_vx.value()
        self.cfg.vel_y = self.in_vy.value()
        self.cfg.vel_z = self.in_vz.value()

    def _sync_cfg_to_widgets(self):
        self.in_scene.setCurrentIndex(self.cfg.scene)
        self.in_rate.setValue(self.cfg.sample_rate_hz)
        self.chk_tdoa.setChecked(bool(self.cfg.enable_mask & P.SPI_MODE_TDOA))
        self.chk_toa.setChecked(bool(self.cfg.enable_mask & P.SPI_MODE_TOA))
        self.chk_aoa.setChecked(bool(self.cfg.enable_mask & P.SPI_MODE_AOA))
        self.chk_rss.setChecked(bool(self.cfg.enable_mask & P.SPI_MODE_RSS))

    # ---------------- 帧/状态 ----------------
    def _on_sent(self, seq: int):
        self._sent += 1
        self._fps_cnt += 1
        self._last_seq = seq
        # 真值轨迹 (从 sim 取当前真值)
        t = self.sender.sim._rw
        self._tx.append(t[0])
        self._ty.append(t[1])
        self._cur_xy = (t[0], t[1])

    def _on_error(self, msg: str):
        self._errors += 1
        self.start_btn.setChecked(False)
        QtWidgets.QMessageBox.warning(self, "发送错误", msg)

    def _refresh_status(self):
        now = time.monotonic()
        dt = now - self._fps_t0
        if dt >= 0.5:
            self._fps = self._fps_cnt / dt
            self._fps_cnt = 0
            self._fps_t0 = now
        # 链路状态灯: 发送中 + 3s 内有板卡应答 = 双向通; 停止/未启动也要刷新
        # (否则残留上次的绿灯, 造成"明明无应答却显示双向通"的假象)
        if self.start_btn.isChecked():
            if self.sender._last_pong_at and (now - self.sender._last_pong_at) < 3.0:
                self.lbl_link.setText("● 双向通 (板端在收)")
                self.lbl_link.setStyleSheet("color:#27ae60")
            else:
                self.lbl_link.setText("● 发送中, 无应答")
                self.lbl_link.setStyleSheet("color:#e67e22")
        else:
            self.lbl_link.setText("● 已停止")
            self.lbl_link.setStyleSheet("color:#888")
        self.lbl_sent.setText(str(self._sent))
        self.lbl_fps.setText(f"{self._fps:.1f} Hz")
        self.lbl_seq.setText(str(self._last_seq))
        self.lbl_err.setText(str(self._errors))
        self.lbl_pong.setText(f"{self.sender._pongs_in} 次")
        self.lbl_rtt.setText(f"{self.sender._last_rtt_ms:.1f} ms"
                             if self.sender._pongs_in else "-")
        self.curve_trail.setData(list(self._tx), list(self._ty))
        if self._tx:
            self.target_dot.setData(
                [{"pos": self._cur_xy, "size": 14,
                  "brush": pg.mkBrush(TRAIL_COLOR), "pen": pg.mkPen("#111", width=1.5)}])

    def _cmd_clear(self):
        self._tx.clear()
        self._ty.clear()
        self._cur_xy = (0.0, 0.0)
        self.curve_trail.setData([], [])
        self.target_dot.setData([])

    def closeEvent(self, e):
        self.sender.close()
        super().closeEvent(e)


def main():
    app = QtWidgets.QApplication(sys.argv)
    w = MainWindow()
    w.show()
    sys.exit(app.exec_())


if __name__ == "__main__":
    main()
