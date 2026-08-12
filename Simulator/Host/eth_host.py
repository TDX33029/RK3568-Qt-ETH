
from __future__ import annotations
import math
import platform
import re
import socket
import struct
import subprocess
import sys
import time
from collections import deque

from PyQt5 import QtCore, QtWidgets
import pyqtgraph as pg

import protocol as P

DEFAULT_TARGET_IP = "192.168.1.10"
DEFAULT_PORT = 5000
SCENE_DRAW = 4                          
POS_TRAIL = 400
ANCHOR_COLOR = "#e74c3c"
TRAIL_COLOR = "#2980b9"
DRAW_COLOR = "#e67e22"                  
HEARTBEAT_MS = 2000        
PING_TIMEOUT_MS = 1500     


def icmp_ping(ip: str, timeout_ms: int = 1500):
    if platform.system() == "Windows":
        
        cmd = ["ping", "-n", "2", "-w", str(timeout_ms), ip]
        kwargs = {"creationflags": 0x08000000}      
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





class DrawPlot(pg.PlotWidget):
    pathChanged = QtCore.pyqtSignal()

    def __init__(self, parent=None, **kargs):
        super().__init__(parent, **kargs)   
        self.draw_pts = []               
        self._drawing = False
        self.draw_enabled = False
        self._path_curve = self.plot(pen=pg.mkPen(DRAW_COLOR, width=3))
        self._path_curve.setVisible(False)

    def set_draw_mode(self, on: bool):
        self.draw_enabled = on
        vb = self.getPlotItem().vb
        vb.setMouseEnabled(not on, not on)      
        self._path_curve.setVisible(on and bool(self.draw_pts))

    def clear_path(self):
        self.draw_pts = []
        self._path_curve.setData([], [])
        self._path_curve.setVisible(False)

    def mousePressEvent(self, ev):
        if self.draw_enabled and ev.button() == QtCore.Qt.LeftButton:
            self._drawing = True
            self._add_point(ev)
            ev.accept()
            return
        super().mousePressEvent(ev)

    def mouseMoveEvent(self, ev):
        if self.draw_enabled and self._drawing:
            self._add_point(ev)
            ev.accept()
            return
        super().mouseMoveEvent(ev)

    def mouseReleaseEvent(self, ev):
        if self._drawing:
            self._drawing = False
            self.pathChanged.emit()
            ev.accept()
            return
        super().mouseReleaseEvent(ev)

    def _add_point(self, ev):
        
        
        scene_pt = self.mapToScene(ev.pos())
        p = self.getPlotItem().vb.mapSceneToView(scene_pt)
        x, y = p.x(), p.y()
        if self.draw_pts:
            dx, dy = x - self.draw_pts[-1][0], y - self.draw_pts[-1][1]
            if dx * dx + dy * dy < 0.01:        
                return
        self.draw_pts.append((x, y))
        self._path_curve.setData([q[0] for q in self.draw_pts],
                                 [q[1] for q in self.draw_pts])
        self._path_curve.setVisible(True)    


class DrawSim:

    def __init__(self, cfg: P.Config, pts, speed: float, z: float = 1.5,
                 anchors=None):
        self.cfg = cfg
        self.pts = list(pts)
        self.z = z
        self.speed = speed                 
        self.anchors = list(anchors) if anchors is not None else list(P.ANCHORS)
        self.n_anc = len(self.anchors)
        self.rng = 0xA5C0FFEE
        
        self._cum = [0.0]
        for i in range(1, len(self.pts)):
            dx = self.pts[i][0] - self.pts[i - 1][0]
            dy = self.pts[i][1] - self.pts[i - 1][1]
            self._cum.append(self._cum[-1] + math.hypot(dx, dy))
        self.total = self._cum[-1] if self.pts else 0.0
        self.reset()

    
    def _xs(self):
        x = self.rng
        x ^= (x << 13) & 0xFFFFFFFF; x ^= (x >> 17); x ^= (x << 5) & 0xFFFFFFFF
        self.rng = x
        return x

    def _frand(self):
        return (self._xs() >> 8) / float(0x00FFFFFF)

    def _gauss(self):
        u1 = max(self._frand(), 1e-7); u2 = self._frand()
        return math.sqrt(-2.0 * math.log(u1)) * math.cos(2.0 * math.pi * u2)

    def reset(self):
        self._s = 0.0
        self._last_us = 0.0
        self._seq = 0

    def _pos_at(self, s):
        if self.total <= 0:
            return 0.0, 0.0, 0.0, 0.0
        s = s % self.total
        for i in range(1, len(self.pts)):
            if s <= self._cum[i]:
                seg = self._cum[i] - self._cum[i - 1]
                t = (s - self._cum[i - 1]) / seg if seg > 1e-9 else 0.0
                x0, y0 = self.pts[i - 1]; x1, y1 = self.pts[i]
                x = x0 + (x1 - x0) * t
                y = y0 + (y1 - y0) * t
                vx = (x1 - x0) / seg * self.speed if seg > 1e-9 else 0.0
                vy = (y1 - y0) / seg * self.speed if seg > 1e-9 else 0.0
                return x, y, vx, vy
        return self.pts[-1][0], self.pts[-1][1], 0.0, 0.0

    def step(self, now_us):
        dt_us = now_us - self._last_us
        self._last_us = now_us
        if dt_us > 1e6:
            dt_us = 1e6
        if dt_us < 0:
            dt_us = 0
        self._s += self.speed * (dt_us * 1e-6)
        x, y, vx, vy = self._pos_at(self._s)
        truth = (x, y, self.z, vx, vy, 0.0)
        return self._pack(x, y, self.z, vx, vy, 0.0, dt_us), truth

    def _pack(self, x, y, z, vx, vy, vz, dt_us):
        cfg = self.cfg
        en = cfg.enable_mask
        rrange = None
        for i in range(self.n_anc):
            ax, ay, az = self.anchors[i]
            dx, dy, dz = x - ax, y - ay, z - az
            rho = math.sqrt(dx * dx + dy * dy) or 1e-3
            rng = math.sqrt(rho * rho + dz * dz) or 1e-3
            if i == 0:
                rrange = rng
                break
        buf = bytearray(P.SPI_FRAME_LEN)
        buf[0] = P.SPI_MAGIC0; buf[1] = P.SPI_MAGIC1
        struct.pack_into("<H", buf, 2, self._seq & 0xFFFF)
        buf[4] = en & 0xFF
        buf[5] = self.n_anc
        struct.pack_into("<I", buf, 6, int(dt_us) & 0xFFFFFFFF)
        off = 12
        for i in range(P.SPI_MAX_ANCHORS):
            if i < self.n_anc:
                ax, ay, az = self.anchors[i]
                dx = x - ax; dy = y - ay; dz = z - az
                rho = math.sqrt(dx * dx + dy * dy) or 1e-3
                rng = math.sqrt(rho * rho + dz * dz) or 1e-3
                has = 0; tdoa = 0.0; toa = 0.0; azm = 0.0; el = 0.0; rss = 0.0
                if (en & P.SPI_MODE_TDOA) and i != P.REF_ANC:
                    has |= P.SPI_MODE_TDOA
                    tdoa = (rng - rrange) / P.C_LIGHT + self._gauss() * P.TDOA_STD
                if en & P.SPI_MODE_TOA:
                    has |= P.SPI_MODE_TOA
                    toa = rng / P.C_LIGHT + self._gauss() * P.TOA_STD
                if en & P.SPI_MODE_AOA:
                    has |= P.SPI_MODE_AOA
                    azm = math.atan2(dy, dx) + self._gauss() * P.AOA_STD
                    el = math.atan2(dz, rho) + self._gauss() * P.AOA_STD
                if en & P.SPI_MODE_RSS:
                    has |= P.SPI_MODE_RSS
                    rss = P.RSS_REF - 10.0 * P.RSS_N * math.log10(rng) + self._gauss() * P.RSS_STD
                struct.pack_into(P.ANCHOR_FMT, buf, off, has, tdoa, toa, azm, el, rss)
            off += P.ANCHOR_SIZE
        struct.pack_into("<H", buf, 180, P.crc16(bytes(buf[:180])))
        self._seq = (self._seq + 1) & 0xFFFF
        return bytes(buf)


class Sender(QtCore.QObject):
    error = QtCore.pyqtSignal(str)
    sent = QtCore.pyqtSignal(int)          
    pong = QtCore.pyqtSignal(int, float)   
    ack = QtCore.pyqtSignal(int)           

    def __init__(self, parent=None):
        super().__init__(parent)
        self.cfg = P.Config()
        self.sim = P.DemoSim(self.cfg)      
        self.sock = None
        self.dest = (DEFAULT_TARGET_IP, DEFAULT_PORT)
        self._last_truth = (0.0, 0.0, 0.0, 0.0, 0.0, 0.0)   
        self._ensure_sock()          
        self._t0 = 0.0
        self._timer = QtCore.QTimer(self)          
        self._timer.timeout.connect(self._tick)
        self._recv = QtCore.QTimer(self)           
        self._recv.setInterval(50)                 
        self._recv.timeout.connect(self._recv_poll)
        self._recv.start()
        self._heartbeat = QtCore.QTimer(self)      
        self._heartbeat.setInterval(HEARTBEAT_MS)
        self._heartbeat.timeout.connect(self.send_ping)

        self._ping_seq = 0
        self._ping_t0 = 0.0
        self._pings_out = 0
        self._pongs_in = 0
        self._last_pong_at = 0.0                   
        self._last_rtt_ms = 0.0
        self._cfg_seq = 0
        self._acks_in = 0
        self._last_ack_at = 0.0                    
        self._last_ack_seq = -1

    def set_dest(self, ip: str, port: int):
        self.dest = (ip, port)

    def apply_config(self, cfg: P.Config):
        self.cfg = cfg

    def set_sim(self, sim):
        self.sim = sim

    def _ensure_sock(self):
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
        
        
        
        self._timer.start(1)
        self.send_ping()                           
        self._heartbeat.start()
        return True

    def stop(self):
        self._timer.stop()
        self._heartbeat.stop()
        

    def close(self):
        self.stop()
        self._recv.stop()
        s = self.sock
        self.sock = None
        if s:
            try: s.close()
            except OSError: pass

    def send_ping(self):
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

    def send_cfg(self, anchors) -> bool:
        if not self._ensure_sock():
            return False
        self._cfg_seq = (self._cfg_seq + 1) & 0xFF
        frame = P.build_cfg_frame(self._cfg_seq, anchors)
        try:
            self.sock.sendto(frame, self.dest)
            return True
        except OSError as e:
            self.error.emit(f"CFG 发送失败: {e}")
            return False

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
                continue
            if P.is_ack_frame(data):               
                self._acks_in += 1
                self._last_ack_at = time.monotonic()
                self._last_ack_seq = data[6]
                self.ack.emit(data[6])

    def _tick(self):
        now_us = int((time.monotonic() - self._t0) * 1e6)
        if now_us < self._next_us:
            return                       
        self._next_us += self._period_us
        if now_us - self._next_us > 1e6:
            self._next_us = now_us       
        frame, truth = self.sim.step(now_us)    
        
        if isinstance(truth, P.Truth):
            truth = (truth.x, truth.y, truth.z, truth.vx, truth.vy, truth.vz)
        self._last_truth = truth
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
        self.setWindowTitle("")
        self.resize(1080, 800)

        self.cfg = P.Config()
        self.sender = Sender(self)
        self.sender.error.connect(self._on_error)
        self.sender.sent.connect(self._on_sent)
        self.sender.pong.connect(self._on_pong)
        self.sender.ack.connect(self._on_ack)

        self._sent = 0
        self._errors = 0
        self._last_seq = 0
        self._fps = 0.0
        self._fps_t0 = time.monotonic()
        self._fps_cnt = 0
        self._ping_btn_pending = False   
        self._udp_rtt = None             
        self._bs_pending = False         

        
        self._tx = deque(maxlen=POS_TRAIL)
        self._ty = deque(maxlen=POS_TRAIL)
        self._cur_xy = (0.0, 0.0)

        self._build_ui()
        self._sync_cfg_to_widgets()

        self._ui_timer = QtCore.QTimer(self)
        self._ui_timer.timeout.connect(self._refresh_status)
        self._ui_timer.start(250)

    
    def _build_ui(self):
        central = QtWidgets.QWidget()
        self.setCentralWidget(central)
        root = QtWidgets.QHBoxLayout(central)

        left = QtWidgets.QFrame()
        left.setFrameShape(QtWidgets.QFrame.StyledPanel)
        left.setMinimumWidth(360)
        left.setMaximumWidth(430)
        ll = QtWidgets.QVBoxLayout(left)
        ll.setSpacing(10)
        root.addWidget(left)
        root.addWidget(self._build_plot(), 1)

        
        g = QtWidgets.QGroupBox("设备")
        f = QtWidgets.QFormLayout(g)
        self.in_ip = QtWidgets.QLineEdit(DEFAULT_TARGET_IP)
        self.in_port = QtWidgets.QSpinBox()
        self.in_port.setRange(1, 65535)
        self.in_port.setValue(DEFAULT_PORT)
        f.addRow("板卡 IP", self.in_ip)
        f.addRow("板卡 PORT", self.in_port)
        ping_row = QtWidgets.QHBoxLayout()
        self.ping_btn = QtWidgets.QPushButton("联通测试")
        self.ping_btn.clicked.connect(self._cmd_ping)
        self.lbl_ping = QtWidgets.QLabel("未测试")
        self.lbl_ping.setWordWrap(True)
        ping_row.addWidget(self.ping_btn)
        ping_row.addWidget(self.lbl_ping, 1)
        f.addRow("", ping_row)
        ll.addWidget(g)

        
        g = QtWidgets.QGroupBox("数据发生配置")
        f = QtWidgets.QFormLayout(g)
        f.setVerticalSpacing(8)
        f.setLabelAlignment(QtCore.Qt.AlignRight | QtCore.Qt.AlignVCenter)
        self.in_scene = QtWidgets.QComboBox()
        for k, v in P.SCENE_NAMES.items():
            if k == P.SCENE_CUSTOM:
                continue
            self.in_scene.addItem(v, k)
        self.in_scene.addItem("自定义路线", SCENE_DRAW)
        self.in_rate = QtWidgets.QSpinBox()
        self.in_rate.setRange(1, 200)
        self.in_rate.setValue(self.cfg.sample_rate_hz)
        mod = QtWidgets.QHBoxLayout()
        mod.setSpacing(6)
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

        
        
        
        self.in_rate.setFixedWidth(110)
        self.in_speed = QtWidgets.QDoubleSpinBox()
        self.in_speed.setRange(0.1, 20.0)
        self.in_speed.setDecimals(1)
        self.in_speed.setSingleStep(0.5)
        self.in_speed.setValue(2.0)
        self.in_speed.setFixedWidth(110)

        f.addRow("模式", self.in_scene)
        f.addRow("采样率 Hz", self.in_rate)
        f.addRow("模态", mod)
        f.addRow("自定义速度 m/s", self.in_speed)
        self.lbl_draw_hint = QtWidgets.QLabel("")
        self.lbl_draw_hint.setStyleSheet("color:#e67e22; font-size:12px;")
        self.lbl_draw_hint.setWordWrap(True)
        f.addRow("", self.lbl_draw_hint)
        ll.addWidget(g)

        self.in_scene.currentIndexChanged.connect(self._on_scene_changed)

        
        g = QtWidgets.QGroupBox("控制台")
        gl = QtWidgets.QGridLayout(g)
        self.start_btn = QtWidgets.QPushButton("启动发送")
        self.stop_btn = QtWidgets.QPushButton("停止")
        self.clear_btn = QtWidgets.QPushButton("清空")
        self.start_btn.setCheckable(True)
        self.start_btn.toggled.connect(self._toggle_send)
        self.stop_btn.clicked.connect(lambda: self.start_btn.setChecked(False))
        self.clear_btn.clicked.connect(self._cmd_clear)
        gl.addWidget(self.start_btn, 0, 0)
        gl.addWidget(self.stop_btn, 0, 1)
        gl.addWidget(self.clear_btn, 0, 2)
        ll.addWidget(g)

        
        g = QtWidgets.QGroupBox("状态 (发/收统计)")
        f = QtWidgets.QFormLayout(g)
        self.lbl_link = QtWidgets.QLabel("未发送")
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
        self.plot = DrawPlot(title="")
        self.plot.showGrid(x=True, y=True, alpha=0.3)
        self.plot.setLabel("bottom", "X", units="m")
        self.plot.setLabel("left", "Y", units="m")
        self.plot.setAspectLocked(True)
        self.curve_trail = self.plot.plot(pen=pg.mkPen(TRAIL_COLOR, width=2))
        self.target_dot = pg.ScatterPlotItem(
            size=14, symbol="o", brush=pg.mkBrush(TRAIL_COLOR), pen=pg.mkPen("#111", width=1.5))
        self.plot.addItem(self.target_dot)
        self.bs_scatter = pg.ScatterPlotItem(
            pos=[(x, y) for x, y, _z in P.ANCHORS],
            size=[16] * P.N_ANC,
            symbol="t1",
            brush=pg.mkBrush(ANCHOR_COLOR),
            pen=pg.mkPen("#7f0f16", width=1.5))
        self.plot.addItem(self.bs_scatter)
        self.bs_labels = []
        for i, (x, y, _z) in enumerate(P.ANCHORS):
            t = pg.TextItem(f"A{i}", color="#c0392b", anchor=(0.5, -0.6))
            t.setPos(x, y)
            self.plot.addItem(t)
            self.bs_labels.append(t)
        v.addWidget(self.plot, 1)
        v.addWidget(self._build_bs_panel())
        return w

    def _build_bs_panel(self):
        g = QtWidgets.QGroupBox("基站 (编辑后进行同步)")
        gl = QtWidgets.QVBoxLayout(g)
        gl.setContentsMargins(8, 6, 8, 6)
        self.bs_table = QtWidgets.QTableWidget(len(P.ANCHORS), 3)
        self.bs_table.setHorizontalHeaderLabels(["X (m)", "Y (m)", "Z (m)"])
        self.bs_table.verticalHeader().setVisible(False)
        self.bs_table.verticalHeader().setDefaultSectionSize(26)   
        self.bs_table.setFixedHeight(26 * len(P.ANCHORS) + 28)     
        for i, (x, y, z) in enumerate(P.ANCHORS):
            for j, val in enumerate((x, y, z)):
                spin = QtWidgets.QDoubleSpinBox()
                spin.setRange(-999.0, 999.0)
                spin.setDecimals(1)
                spin.setSingleStep(0.5)
                spin.setValue(val)
                spin.valueChanged.connect(self._on_bs_edited)
                self.bs_table.setCellWidget(i, j, spin)
        self.bs_table.horizontalHeader().setSectionResizeMode(
            QtWidgets.QHeaderView.Stretch)
        gl.addWidget(self.bs_table)
        row = QtWidgets.QHBoxLayout()
        self.bs_sync_btn = QtWidgets.QPushButton("应用到板端")
        self.bs_sync_btn.clicked.connect(self._apply_bs)
        self.lbl_bs_status = QtWidgets.QLabel("未同步")
        self.lbl_bs_status.setStyleSheet("color:#888; font-size:12px;")
        row.addWidget(self.bs_sync_btn)
        row.addWidget(self.lbl_bs_status, 1)
        gl.addLayout(row)
        return g

    
    def _collect_anchors(self):
        pts = []
        for i in range(self.bs_table.rowCount()):
            vals = []
            for j in range(3):
                w = self.bs_table.cellWidget(i, j)
                if w is None:
                    vals.append(0.0)
                else:
                    vals.append(w.value())
            pts.append(tuple(vals))
        return pts

    def _update_bs_plot(self):
        pts = self._collect_anchors()
        self.bs_scatter.setData(pos=[(p[0], p[1]) for p in pts])
        for i, t in enumerate(self.bs_labels):
            if i < len(pts):
                t.setPos(pts[i][0], pts[i][1])
                t.setVisible(True)
            else:
                t.setVisible(False)

    def _on_bs_edited(self, _v):
        self._update_bs_plot()
        self.lbl_bs_status.setText("已编辑, 未同步")
        self.lbl_bs_status.setStyleSheet("color:#e67e22; font-size:12px;")

    def _apply_bs(self):
        anchors = self._collect_anchors()
        if self.sender.send_cfg(anchors):
            self._bs_pending = True
            self.lbl_bs_status.setText("已发送, 等待板端确认…")
            self.lbl_bs_status.setStyleSheet("color:#e67e22; font-size:12px;")
            QtCore.QTimer.singleShot(3000, self._check_bs_ack)

    def _check_bs_ack(self):
        if self._bs_pending:
            self.lbl_bs_status.setText("未收到确认 — 板端未运行 / 未部署含 CFG 的新代码")
            self.lbl_bs_status.setStyleSheet("color:#C44E35; font-size:12px;")

    def _on_ack(self, seq: int):
        self._bs_pending = False
        self.lbl_bs_status.setText(f"板端已确认 (seq={seq})")
        self.lbl_bs_status.setStyleSheet("color:#27ae60; font-size:12px;")

    
    def _is_draw_scene(self):
        return self.in_scene.currentData() == SCENE_DRAW

    def _on_scene_changed(self, _idx: int):
        if self._is_draw_scene():
            self.plot.set_draw_mode(True)
            self.lbl_draw_hint.setText("")
        else:
            self.plot.set_draw_mode(False)
            self.lbl_draw_hint.setText("")

    
    def _toggle_send(self, on: bool):
        if on:
            self._apply_widgets_to_cfg()
            self.sender.set_dest(self.in_ip.text().strip(), self.in_port.value())
            self.sender.apply_config(self.cfg)
            
            anchors = self._collect_anchors()
            if self._is_draw_scene():
                pts = self.plot.draw_pts
                if len(pts) < 2:
                    QtWidgets.QMessageBox.warning(
                        self, "轨迹错误",
                        "无效绘制")
                    self.start_btn.setChecked(False)
                    return
                sim = DrawSim(self.cfg, pts, self.in_speed.value(), z=1.5,
                              anchors=anchors)
            else:
                sim = P.DemoSim(self.cfg, anchors=anchors)
            self.sender.set_sim(sim)
            if not self.sender.start():
                self.start_btn.setChecked(False)
                return
            self._sent = 0
            self._errors = 0
            self._fps_cnt = 0
            self._fps_t0 = time.monotonic()
        else:
            self.sender.stop()
            self.lbl_link.setText("已停止")
            self.lbl_link.setStyleSheet("color:#888")

    
    def _cmd_ping(self):
        ip = self.in_ip.text().strip()
        self.lbl_ping.setText("检测中… (ICMP + UDP)")
        self.ping_btn.setEnabled(False)
        self._ping_btn_pending = True
        self._udp_rtt = None
        self.sender.send_ping()

        
        def finish():
            self.ping_btn.setEnabled(True)
            self._ping_btn_pending = False
            if self._udp_rtt is not None:
                self.lbl_ping.setText(
                    f"联通 RTT={self._udp_rtt:.1f} ms")
            else:
                self.lbl_ping.setText(
                    f"{self._icmp_desc}"
                    "应确认程序状态")
        QtCore.QTimer.singleShot(PING_TIMEOUT_MS, finish)

        
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

    def _sync_cfg_to_widgets(self):
        self.in_scene.setCurrentIndex(self.cfg.scene)
        self.in_rate.setValue(self.cfg.sample_rate_hz)
        self.chk_tdoa.setChecked(bool(self.cfg.enable_mask & P.SPI_MODE_TDOA))
        self.chk_toa.setChecked(bool(self.cfg.enable_mask & P.SPI_MODE_TOA))
        self.chk_aoa.setChecked(bool(self.cfg.enable_mask & P.SPI_MODE_AOA))
        self.chk_rss.setChecked(bool(self.cfg.enable_mask & P.SPI_MODE_RSS))

    
    def _on_sent(self, seq: int):
        self._sent += 1
        self._fps_cnt += 1
        self._last_seq = seq
        
        t = self.sender._last_truth
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
        
        
        if self.start_btn.isChecked():
            if self.sender._last_pong_at and (now - self.sender._last_pong_at) < 3.0:
                self.lbl_link.setText("联通")
                self.lbl_link.setStyleSheet("color:#27ae60")
            else:
                self.lbl_link.setText("无应答")
                self.lbl_link.setStyleSheet("color:#e67e22")
        else:
            self.lbl_link.setText("已停止")
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
        if self._is_draw_scene():
            self.plot.clear_path()      

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
