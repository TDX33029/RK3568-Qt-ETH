"""
host_gui.py - F407 仿真传感器上位机

功能：
  * 串口（UART）连接 F407，下发配置 / 启停，接收 TOA/AOA/TDOA 数据帧并实时绘图。
  * "Demo 模式"：无硬件时本地按与固件一致的运动模型生成数据，可独立演示。
  * 同一协议帧在 UART 与 SPI 链路上字节一致（见 protocol.py）。

依赖： PyQt5, pyqtgraph, pyserial  （pip install -r requirements.txt）
运行： python host_gui.py
"""
from __future__ import annotations
import sys
import time
import struct
from collections import deque

import serial
import serial.tools.list_ports

from PyQt5 import QtCore, QtGui, QtWidgets
import pyqtgraph as pg

import protocol as P

# ---- 绘图配置 ----
PLOT_WINDOW = 1000           # 绘图保留点数
PLOT_RATE_HZ = 30            # 刷新频率
POS_TRAIL = 300              # 位置轨迹保留点数
# 默认基站 (x_m, y_m, name)
DEFAULT_BASE_STATIONS = [
    (-300.0, -300.0, "BS0"),
    ( 300.0, -300.0, "BS1"),
    ( 300.0,  300.0, "BS2"),
    (-300.0,  300.0, "BS3"),
]
TARGET_COLORS = ["#56B4E9", "#E69F00", "#009E73", "#CC79A7",
                 "#F0E442", "#0072B2", "#D55E00", "#999999"]

MOTION_NAMES = {0: "静止 Static", 1: "直线 Linear",
                2: "圆周 Circular", 3: "随机游走 Random-Walk"}


# ============================================================
#  本地演示数据源（与固件 sim.c 运动模型一致）
# ============================================================
class DemoGenerator(QtCore.QObject):
    frame_ready = QtCore.pyqtSignal(int, int, int, bytes)   # type, seq, ts, payload
    status_tick = QtCore.pyqtSignal(dict)

    def __init__(self, cfg: P.Config):
        super().__init__()
        self.cfg = cfg
        self.frame_index = 0
        self.seq = 0
        self.t0 = time.monotonic()
        self._rng_state = 0xA5C0FFEE
        self._rw = [0.5] * 8
        self._phase = [i / 8.0 for i in range(8)]
        self._px = [i * 120.0 - 360.0 for i in range(8)]
        self._py = [i * 80.0 - 240.0 for i in range(8)]
        self._timer = QtCore.QTimer(self)
        self._timer.timeout.connect(self._tick)

    def start(self):
        rate = max(1, self.cfg.sample_rate_hz)
        self._timer.start(int(1000 / rate))

    def stop(self):
        self._timer.stop()

    def set_config(self, cfg: P.Config):
        self.cfg = cfg
        if self._timer.isActive():
            self.stop()
            self.start()

    def reset(self):
        self.frame_index = 0
        self.seq = 0
        for i in range(8):
            self._rw[i] = 0.5
            self._px[i] = i * 120.0 - 360.0
            self._py[i] = i * 80.0 - 240.0

    def _xorshift(self):
        x = self._rng_state
        x ^= (x << 13) & 0xFFFFFFFF
        x ^= (x >> 17)
        x ^= (x << 5) & 0xFFFFFFFF
        self._rng_state = x
        return x

    def _frand(self):
        return (self._xorshift() >> 8) / float(0x00FFFFFF)

    def _gauss(self):
        import math
        u1 = max(self._frand(), 1e-7)
        u2 = self._frand()
        return math.sqrt(-2.0 * math.log(u1)) * math.cos(2.0 * 3.14159265358979 * u2)

    def _motion(self, idx):
        import math
        cfg = self.cfg
        period_s = max(cfg.motion_period_ms / 1000.0, 1e-3)
        t = self.frame_index / max(cfg.sample_rate_hz, 1)
        phase = t / period_s + self._phase[idx]
        if cfg.motion_model == 0:
            return 0.5
        elif cfg.motion_model == 1:
            return phase - math.floor(phase)
        elif cfg.motion_model == 2:
            return 0.5 + 0.5 * math.sin(2 * math.pi * phase)
        else:
            self._rw[idx] += 0.02 * self._gauss()
            self._rw[idx] = max(0.0, min(1.0, self._rw[idx]))
            return self._rw[idx]

    def _motion_pos(self, idx, n, fi):
        import math
        cfg = self.cfg
        rate = max(cfg.sample_rate_hz, 1)
        period_s = max(cfg.motion_period_ms / 1000.0, 1e-3)
        t = fi / rate
        phase = t / period_s + idx / n
        R = 150.0 + 60.0 * idx
        if cfg.motion_model == 0:                 # static
            x = idx * 150.0 - (n - 1) * 75.0
            y = 0.0
        elif cfg.motion_model == 1:               # linear
            frac = phase - math.floor(phase)
            x = (frac - 0.5) * 600.0
            y = (idx - (n - 1) * 0.5) * 120.0
        elif cfg.motion_model == 2:               # circular
            x = R * math.sin(2 * math.pi * phase)
            y = R * math.cos(2 * math.pi * phase)
        else:                                     # random walk
            self._px[idx] += 5.0 * self._gauss()
            self._py[idx] += 5.0 * self._gauss()
            self._px[idx] = max(-300.0, min(300.0, self._px[idx]))
            self._py[idx] = max(-300.0, min(300.0, self._py[idx]))
            x = self._px[idx]; y = self._py[idx]
        x = max(-32768, min(32767, int(x)))
        y = max(-32768, min(32767, int(y)))
        return x, y

    def _tick(self):
        cfg = self.cfg
        n = max(1, min(cfg.n_targets, 8))
        sigma = cfg.noise_sigma_x10 * 0.1
        ts = int((time.monotonic() - self.t0) * 1000)
        hdr = struct.pack(P.DF_HDR_FMT, n, 1, 0, self.frame_index, ts)
        body = hdr
        for i in range(n):
            m = self._motion(i)
            g = self._gauss() * sigma if sigma > 0 else 0.0
            toa = self._clamp(int(cfg.toa_base_ns + (m - 0.5) * cfg.toa_span_ns + g), 0, 65535)
            az = self._clamp(int(cfg.aoa_center_deg_x10 + (m - 0.5) * cfg.aoa_span_deg_x10 + g), -32768, 32767)
            el = self._clamp(int((m - 0.5) * cfg.aoa_span_deg_x10 * 0.3 + g * 0.5), -32768, 32767)
            tdoa = self._clamp(int(cfg.tdoa_center_ns_x10 + (m - 0.5) * cfg.tdoa_span_ns_x10 + g), -32768, 32767)
            snr = self._clamp(int(38000 + g * 2000), 0, 65535)
            px, py = self._motion_pos(i, n, self.frame_index)
            body += struct.pack(P.TARGET_FMT, toa, az, el, tdoa, snr, px, py)
        frame = P.build_frame(P.RSP_DATA_FRAME, self.seq, ts, body)
        self.seq = (self.seq + 1) & 0xFFFF
        self.frame_ready.emit(P.RSP_DATA_FRAME, self.seq - 1, ts, body)
        self.frame_index += 1

    @staticmethod
    def _clamp(v, lo, hi):
        return lo if v < lo else (hi if v > hi else v)


# ============================================================
#  串口读线程
# ============================================================
class SerialWorker(QtCore.QObject):
    frame_ready = QtCore.pyqtSignal(int, int, int, bytes)   # type, seq, ts, payload
    raw_bytes = QtCore.pyqtSignal(bytes)
    crc_error = QtCore.pyqtSignal()
    log = QtCore.pyqtSignal(str)
    connection_changed = QtCore.pyqtSignal(bool)

    def __init__(self):
        super().__init__()
        self.ser = None
        self._stop = False          # 仅应用退出时置位，停止 run 循环
        self._parser = P.FrameParser()
        self._lock = QtCore.QMutex()

    def open(self, port, baud):
        # 在主线程直接调用；打开串口很快。失败时发日志，不会静默。
        try:
            self.ser = serial.Serial(port, baud, timeout=0.05,
                                     write_timeout=0.2)
            self.connection_changed.emit(True)
            self.log.emit(f"已连接 {port} @ {baud}")
            return True
        except Exception as e:
            self.ser = None
            self.connection_changed.emit(False)
            self.log.emit(f"连接失败: {e}")
            return False

    def close(self):
        # 用户断开：只关串口，run 循环继续空转等待下次 open
        s = self.ser
        self.ser = None
        if s:
            try:
                s.close()
            except Exception:
                pass
            self.connection_changed.emit(False)
            self.log.emit("已断开")

    def send(self, data: bytes):
        s = self.ser
        if not s:
            return
        self._lock.lock()
        try:
            s.write(data)
        except Exception as e:
            self.log.emit(f"发送失败: {e}")
        finally:
            self._lock.unlock()

    def run(self):
        while not self._stop:
            s = self.ser                     # 本地快照，避免与 close/open 竞争
            if s is None:
                QtCore.QThread.msleep(50)
                continue
            try:
                data = s.read(1024)
            except Exception as e:
                self.log.emit(f"读取异常: {e}")
                QtCore.QThread.msleep(100)
                continue
            if not data:
                continue
            self.raw_bytes.emit(data)
            for item in self._parser.feed(data):
                if item[0] == "CRC_ERROR":
                    self.crc_error.emit()
                else:
                    self.frame_ready.emit(*item)


# ============================================================
#  主窗口
# ============================================================
class MainWindow(QtWidgets.QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("F407 仿真传感器上位机 - TOA/AOA/TDOA")
        self.resize(1280, 820)

        self.cfg = P.Config()
        self.demo = DemoGenerator(self.cfg)
        self.demo.frame_ready.connect(self._on_frame)
        self.worker = SerialWorker()
        self.worker.frame_ready.connect(self._on_frame)
        self.worker.crc_error.connect(self._on_crc_error)
        self.worker.raw_bytes.connect(self._on_raw)
        self.worker.log.connect(self._on_log)
        self.worker.connection_changed.connect(self._on_conn_changed)

        self.thread = QtCore.QThread(self)
        self.worker.moveToThread(self.thread)
        self.thread.started.connect(self.worker.run)
        self.thread.start()

        self._seq = 0
        self._frame_count = 0
        self._crc_errors = 0
        self._connected = False
        self._demo_on = False
        self._rx_count = 0          # 墙钟计数接收帧数（独立于 F407 时间戳）
        self._rx_t0 = time.monotonic()
        self._rx_fps = 0.0

        # 绘图缓冲：metric -> list(deque) per target
        self._t_buf = deque(maxlen=PLOT_WINDOW)
        self._toa = [deque(maxlen=PLOT_WINDOW) for _ in range(8)]
        self._az = [deque(maxlen=PLOT_WINDOW) for _ in range(8)]
        self._el = [deque(maxlen=PLOT_WINDOW) for _ in range(8)]
        self._tdoa = [deque(maxlen=PLOT_WINDOW) for _ in range(8)]
        # 2D 位置轨迹与当前点
        self._posx = [deque(maxlen=POS_TRAIL) for _ in range(8)]
        self._posy = [deque(maxlen=POS_TRAIL) for _ in range(8)]
        self._cur_pos = [[0, 0] for _ in range(8)]
        # 基站位置（GUI 端配置，仅显示参考）
        self._base_stations = list(DEFAULT_BASE_STATIONS)
        self._t0 = time.monotonic()

        self._build_ui()
        self._sync_cfg_to_widgets()

        self._plot_timer = QtCore.QTimer(self)
        self._plot_timer.timeout.connect(self._refresh_plots)
        self._plot_timer.start(int(1000 / PLOT_RATE_HZ))

    # ---------------- UI 构建 ----------------
    def _build_ui(self):
        central = QtWidgets.QWidget()
        self.setCentralWidget(central)
        root = QtWidgets.QHBoxLayout(central)

        left = QtWidgets.QFrame()
        left.setFrameShape(QtWidgets.QFrame.StyledPanel)
        left.setMinimumWidth(330)
        left.setMaximumWidth(360)
        ll = QtWidgets.QVBoxLayout(left)
        root.addWidget(left)
        root.addWidget(self._build_plots(), 1)
        right = self._build_right()
        root.addWidget(right, 0)

        self._build_connection_box(ll)
        self._build_config_box(ll)
        self._build_control_box(ll)
        self._build_base_box(ll)
        ll.addStretch(1)
        self._build_status_box(ll)

    def _build_connection_box(self, ll):
        g = QtWidgets.QGroupBox("连接 Connection")
        gl = QtWidgets.QGridLayout(g)
        self.port_combo = QtWidgets.QComboBox()
        self.port_combo.setEditable(True)
        self.refresh_btn = QtWidgets.QPushButton("刷新")
        self.refresh_btn.clicked.connect(self._refresh_ports)
        gl.addWidget(QtWidgets.QLabel("端口 Port"), 0, 0)
        gl.addWidget(self.port_combo, 0, 1)
        gl.addWidget(self.refresh_btn, 0, 2)

        self.baud_combo = QtWidgets.QComboBox()
        self.baud_combo.addItems(["115200", "230400", "460800", "921600", "9600"])
        gl.addWidget(QtWidgets.QLabel("波特 Baud"), 1, 0)
        gl.addWidget(self.baud_combo, 1, 1, 1, 2)

        self.connect_btn = QtWidgets.QPushButton("连接 Connect")
        self.connect_btn.clicked.connect(self._toggle_connect)
        self.demo_btn = QtWidgets.QPushButton("启动 Demo 模式")
        self.demo_btn.setCheckable(True)
        self.demo_btn.toggled.connect(self._toggle_demo)
        hb = QtWidgets.QHBoxLayout()
        hb.addWidget(self.connect_btn)
        hb.addWidget(self.demo_btn)
        gl.addLayout(hb, 2, 0, 1, 3)
        ll.addWidget(g)
        self._refresh_ports()

    def _build_config_box(self, ll):
        g = QtWidgets.QGroupBox("仿真配置 Config")
        f = QtWidgets.QFormLayout(g)
        self.in_rate = QtWidgets.QSpinBox(); self.in_rate.setRange(1, 5000); self.in_rate.setValue(100)
        self.in_ntgt = QtWidgets.QSpinBox(); self.in_ntgt.setRange(1, 8); self.in_ntgt.setValue(1)
        self.in_motion = QtWidgets.QComboBox()
        for k, v in MOTION_NAMES.items():
            self.in_motion.addItem(v, k)
        self.in_period = QtWidgets.QSpinBox(); self.in_period.setRange(1, 60000); self.in_period.setValue(2000); self.in_period.setSuffix(" ms")
        self.in_toa_base = QtWidgets.QSpinBox(); self.in_toa_base.setRange(0, 65535); self.in_toa_base.setValue(10000)
        self.in_toa_span = QtWidgets.QSpinBox(); self.in_toa_span.setRange(0, 65535); self.in_toa_span.setValue(20000)
        self.in_aoa_ctr = QtWidgets.QSpinBox(); self.in_aoa_ctr.setRange(-32000, 32000); self.in_aoa_ctr.setValue(0)
        self.in_aoa_span = QtWidgets.QSpinBox(); self.in_aoa_span.setRange(0, 64000); self.in_aoa_span.setValue(600)
        self.in_tdoa_ctr = QtWidgets.QSpinBox(); self.in_tdoa_ctr.setRange(-32000, 32000); self.in_tdoa_ctr.setValue(0)
        self.in_tdoa_span = QtWidgets.QSpinBox(); self.in_tdoa_span.setRange(0, 64000); self.in_tdoa_span.setValue(5000)
        self.in_noise = QtWidgets.QDoubleSpinBox(); self.in_noise.setRange(0, 1000); self.in_noise.setValue(2.0); self.in_noise.setSingleStep(0.1); self.in_noise.setSuffix(" LSB")
        self.in_decim = QtWidgets.QSpinBox(); self.in_decim.setRange(0, 255); self.in_decim.setValue(1)
        f.addRow("采样率 Rate (Hz)", self.in_rate)
        f.addRow("目标数 Targets", self.in_ntgt)
        f.addRow("运动模型 Motion", self.in_motion)
        f.addRow("运动周期 Period", self.in_period)
        f.addRow("TOA 基准", self.in_toa_base)
        f.addRow("TOA 量程", self.in_toa_span)
        f.addRow("AOA 中心(°×10)", self.in_aoa_ctr)
        f.addRow("AOA 量程(°×10)", self.in_aoa_span)
        f.addRow("TDOA 中心", self.in_tdoa_ctr)
        f.addRow("TDOA 量程", self.in_tdoa_span)
        f.addRow("噪声 σ", self.in_noise)
        f.addRow("遥测抽稀 Decim", self.in_decim)
        self.apply_cfg_btn = QtWidgets.QPushButton("下发配置 Send Config")
        self.apply_cfg_btn.clicked.connect(self._send_config)
        f.addRow(self.apply_cfg_btn)
        ll.addWidget(g)

    def _build_control_box(self, ll):
        g = QtWidgets.QGroupBox("控制 Control")
        gl = QtWidgets.QGridLayout(g)
        self.start_btn = QtWidgets.QPushButton("▶ Start")
        self.stop_btn = QtWidgets.QPushButton("■ Stop")
        self.clear_btn = QtWidgets.QPushButton("🧹 清空 Clear")
        self.ping_btn = QtWidgets.QPushButton("Ping")
        self.reset_btn = QtWidgets.QPushButton("Reset Seq")
        self.status_btn = QtWidgets.QPushButton("Get Status")
        self.start_btn.clicked.connect(self._cmd_start)
        self.stop_btn.clicked.connect(self._cmd_stop)
        self.clear_btn.clicked.connect(self._cmd_clear)
        self.ping_btn.clicked.connect(self._cmd_ping)
        self.reset_btn.clicked.connect(self._cmd_reset)
        self.status_btn.clicked.connect(self._cmd_status)
        gl.addWidget(self.start_btn, 0, 0); gl.addWidget(self.stop_btn, 0, 1)
        gl.addWidget(self.clear_btn, 0, 2)
        gl.addWidget(self.ping_btn, 1, 0); gl.addWidget(self.reset_btn, 1, 1)
        gl.addWidget(self.status_btn, 1, 2)
        ll.addWidget(g)

    def _build_status_box(self, ll):
        g = QtWidgets.QGroupBox("状态 Status")
        f = QtWidgets.QFormLayout(g)
        self.lbl_conn = QtWidgets.QLabel("● 离线")
        self.lbl_conn.setStyleSheet("color:#888")
        self.lbl_frames = QtWidgets.QLabel("0")
        self.lbl_rate = QtWidgets.QLabel("-")
        self.lbl_crc = QtWidgets.QLabel("0")
        self.lbl_remote = QtWidgets.QLabel("-")
        f.addRow("链路", self.lbl_conn)
        f.addRow("接收帧数", self.lbl_frames)
        f.addRow("实时速率", self.lbl_rate)
        f.addRow("CRC 错误", self.lbl_crc)
        f.addRow("远端状态", self.lbl_remote)
        ll.addWidget(g)

    def _build_base_box(self, ll):
        g = QtWidgets.QGroupBox("基站 Base Stations (m)")
        v = QtWidgets.QVBoxLayout(g)
        self.bs_edit = QtWidgets.QPlainTextEdit()
        self.bs_edit.setPlainText(
            "\n".join(f"{x:.0f}, {y:.0f}" for x, y, _ in DEFAULT_BASE_STATIONS))
        self.bs_edit.setFixedHeight(90)
        v.addWidget(self.bs_edit)
        hb = QtWidgets.QHBoxLayout()
        self.bs_apply_btn = QtWidgets.QPushButton("应用 Apply")
        self.bs_apply_btn.clicked.connect(self._apply_base_stations)
        hb.addStretch(1); hb.addWidget(self.bs_apply_btn)
        v.addLayout(hb)
        ll.addWidget(g)

    def _apply_base_stations(self):
        pts = []
        for ln in self.bs_edit.toPlainText().splitlines():
            ln = ln.strip()
            if not ln:
                continue
            parts = ln.replace(",", " ").split()
            if len(parts) >= 2:
                try:
                    pts.append((float(parts[0]), float(parts[1])))
                except ValueError:
                    self._on_log(f"基站行解析失败: {ln!r}")
        if pts:
            self._base_stations = [(x, y, f"BS{i}") for i, (x, y) in enumerate(pts)]
            self._update_base_stations()
            self._on_log(f"已更新 {len(self._base_stations)} 个基站")

    def _update_base_stations(self):
        spots = [{"pos": (x, y), "data": name}
                 for x, y, name in self._base_stations]
        self.bs_scatter.setData(spots)
        # 清除旧文字并重建
        for lbl in self.bs_labels:
            self.plot_pos.removeItem(lbl)
        self.bs_labels = []
        for x, y, name in self._base_stations:
            t = pg.TextItem(name, color="#c0392b", anchor=(0.5, -0.5))
            t.setPos(x, y)
            self.plot_pos.addItem(t)
            self.bs_labels.append(t)

    def _build_plots(self):
        w = QtWidgets.QWidget()
        grid = QtWidgets.QGridLayout(w)
        grid.setSpacing(4)
        pg.setConfigOptions(antialias=True)

        self.plot_toa = pg.PlotWidget(title="TOA vs 时间 (per target)")
        self.plot_az = pg.PlotWidget(title="AOA 方位/俯仰 vs 时间")
        self.plot_tdoa = pg.PlotWidget(title="TDOA vs 时间")
        self.plot_pos = pg.PlotWidget(title="实时位置 Position + 基站 BaseStations")
        for p in (self.plot_toa, self.plot_az, self.plot_tdoa):
            p.showGrid(x=True, y=True, alpha=0.3)
            p.setLabel("bottom", "时间", units="s")
            p.addLegend(offset=(10, 10))
        # 位置图：等比例、网格、坐标轴
        self.plot_pos.showGrid(x=True, y=True, alpha=0.3)
        self.plot_pos.setLabel("bottom", "X", units="m")
        self.plot_pos.setLabel("left", "Y", units="m")
        self.plot_pos.setAspectLocked(True)
        self.plot_pos.addLegend(offset=(10, 10))

        grid.addWidget(self.plot_toa, 0, 0)
        grid.addWidget(self.plot_az, 0, 1)
        grid.addWidget(self.plot_tdoa, 1, 0)
        grid.addWidget(self.plot_pos, 1, 1)

        self.curves_toa = []
        self.curves_az = []
        self.curves_el = []
        self.curves_tdoa = []
        self.curves_pos = []
        for i in range(8):
            c = TARGET_COLORS[i]
            self.curves_toa.append(self.plot_toa.plot(pen=pg.mkPen(c, width=1.5), name=f"T{i} TOA"))
            self.curves_az.append(self.plot_az.plot(pen=pg.mkPen(c, width=1.5), name=f"T{i} az"))
            self.curves_el.append(self.plot_az.plot(pen=pg.mkPen(pg.QtGui.QColor(c), width=1.0, style=QtCore.Qt.DashLine), name=f"T{i} el"))
            self.curves_tdoa.append(self.plot_tdoa.plot(pen=pg.mkPen(c, width=1.5), name=f"T{i} TDOA"))
            self.curves_pos.append(self.plot_pos.plot(pen=pg.mkPen(c, width=1.3), name=f"T{i} trail"))

        # 基站标记（红色三角）+ 文字
        self.bs_scatter = pg.ScatterPlotItem(size=15, symbol="t1",
                                             brush=pg.mkBrush("#e74c3c"),
                                             pen=pg.mkPen("#7f0f16", width=1.5),
                                             name="基站")
        self.plot_pos.addItem(self.bs_scatter)
        self.bs_labels = []
        # 目标当前位置（圆点）
        self.target_scatter = pg.ScatterPlotItem(size=12, symbol="o",
                                                 brush=pg.mkBrush("#ffffff"),
                                                 pen=pg.mkPen("#222222", width=1.5),
                                                 name="目标")
        self.plot_pos.addItem(self.target_scatter)
        self._update_base_stations()
        return w

    def _build_right(self):
        w = QtWidgets.QWidget(); w.setMaximumWidth(440)
        l = QtWidgets.QVBoxLayout(w)
        l.addWidget(QtWidgets.QLabel("最新数据 Latest Frame"))
        self.table = QtWidgets.QTableWidget(8, 8)
        self.table.setHorizontalHeaderLabels(
            ["tgt", "TOA", "AOA_az", "AOA_el", "TDOA", "SNR", "PosX", "PosY"])
        self.table.verticalHeader().setVisible(False)
        for r in range(8):
            for c in range(8):
                self.table.setItem(r, c, QtWidgets.QTableWidgetItem(""))
        self.table.horizontalHeader().setSectionResizeMode(QtWidgets.QHeaderView.Stretch)
        l.addWidget(self.table)

        l.addWidget(QtWidgets.QLabel("原始帧 / 日志 Raw / Log"))
        self.log_view = QtWidgets.QPlainTextEdit()
        self.log_view.setReadOnly(True)
        self.log_view.setMaximumBlockCount(2000)
        l.addWidget(self.log_view, 1)
        return w

    # ---------------- 信号处理 ----------------
    def _on_frame(self, ftype, seq, ts, payload):
        if ftype == P.RSP_DATA_FRAME:
            self._frame_count += 1
            try:
                df = P.parse_data_frame(payload)
            except Exception as e:
                self._on_log(f"解析 DATA 帧失败: {e}")
                return
            self._on_data_frame(df)
        elif ftype == P.RSP_PONG:
            self._on_log(f"PONG seq={seq}: {payload!r}")
        elif ftype == P.RSP_ACK:
            if len(payload) >= 2:
                self._on_log(f"ACK cmd=0x{payload[0]:02X} stat=0x{payload[1]:02X}")
        elif ftype == P.RSP_NACK:
            if len(payload) >= 2:
                self._on_log(f"NACK cmd=0x{payload[0]:02X} stat=0x{payload[1]:02X}")
        elif ftype == P.RSP_STATUS:
            try:
                st = P.Status.unpack(payload)
                self.lbl_remote.setText(
                    f"{'运行' if st.running else '停止'} | {st.n_targets}tgt | "
                    f"{st.sample_rate_hz}Hz | 帧{st.frame_count} | "
                    f"SPI溢{st.spi_overrun} | UART溢{st.uart_overrun} | "
                    f"CRC{st.crc_errors} | cmd{st.cmd_count}")
            except Exception as e:
                self._on_log(f"解析 STATUS 失败: {e}")
        elif ftype == P.RSP_LOG:
            self._on_log(f"[LOG] {payload.decode('utf-8', 'replace')}")

    def _on_data_frame(self, df: P.DataFrame):
        self._rx_count += 1
        t = (df.timestamp_ms / 1000.0) if df.timestamp_ms else (time.monotonic() - self._t0)
        self._t_buf.append(t)
        for i in range(8):
            if i < df.n_targets:
                tg = df.targets[i]
                self._toa[i].append(tg.toa)
                self._az[i].append(tg.aoa_az)
                self._el[i].append(tg.aoa_el)
                self._tdoa[i].append(tg.tdoa)
                self._posx[i].append(tg.pos_x)
                self._posy[i].append(tg.pos_y)
                self._cur_pos[i] = [tg.pos_x, tg.pos_y]
                if i < self.table.rowCount():
                    self.table.item(i, 0).setText(str(i))
                    self.table.item(i, 1).setText(str(tg.toa))
                    self.table.item(i, 2).setText(str(tg.aoa_az))
                    self.table.item(i, 3).setText(str(tg.aoa_el))
                    self.table.item(i, 4).setText(str(tg.tdoa))
                    self.table.item(i, 5).setText(str(tg.snr))
                    self.table.item(i, 6).setText(str(tg.pos_x))
                    self.table.item(i, 7).setText(str(tg.pos_y))
            else:
                # 目标未到：补 NaN 占位保持对齐
                for buf in (self._toa[i], self._az[i], self._el[i], self._tdoa[i]):
                    buf.append(float('nan'))
                self._posx[i].append(float('nan'))
                self._posy[i].append(float('nan'))

    def _on_crc_error(self):
        self._crc_errors += 1

    def _on_raw(self, data: bytes):
        # 仅在需要看原始流时可扩展；这里不逐字节打印避免刷屏
        pass

    def _on_log(self, msg: str):
        self.log_view.appendPlainText(f"[{time.strftime('%H:%M:%S')}] {msg}")

    def _on_conn_changed(self, ok: bool):
        self._connected = ok
        self.lbl_conn.setText("● 已连接" if ok else "● 离线")
        self.lbl_conn.setStyleSheet("color:#27ae60" if ok else "color:#888")
        self.connect_btn.setText("断开 Disconnect" if ok else "连接 Connect")
        if ok:
            # 连上即 Ping 一次，验证 F407 是否应答（日志会显示 PONG）
            self._send_frame(P.CMD_PING)

    # ---------------- 刷新绘图 ----------------
    def _refresh_plots(self):
        self.lbl_frames.setText(str(self._frame_count))
        self.lbl_crc.setText(str(self._crc_errors))
        # 墙钟接收速率（每 0.5s 刷新），独立于 F407 时间戳
        now = time.monotonic()
        dt = now - self._rx_t0
        if dt >= 0.5:
            self._rx_fps = self._rx_count / dt
            self._rx_count = 0
            self._rx_t0 = now
        self.lbl_rate.setText(f"{self._rx_fps:.1f} Hz")
        xs = list(self._t_buf)
        for i in range(8):
            self.curves_toa[i].setData(xs, list(self._toa[i]), connect="finite")
            self.curves_az[i].setData(xs, list(self._az[i]), connect="finite")
            self.curves_el[i].setData(xs, list(self._el[i]), connect="finite")
            self.curves_tdoa[i].setData(xs, list(self._tdoa[i]), connect="finite")
            self.curves_pos[i].setData(list(self._posx[i]), list(self._posy[i]), connect="finite")
        # 目标当前点（每目标一色）
        spots = []
        for i in range(8):
            if len(self._posx[i]) > 0:
                x = self._posx[i][-1]; y = self._posy[i][-1]
                if not (x != x):   # not NaN
                    spots.append({"pos": (x, y), "size": 13,
                                  "brush": pg.mkBrush(TARGET_COLORS[i]),
                                  "pen": pg.mkPen("#222222", width=1)})
        self.target_scatter.setData(spots)

    # ---------------- 串口 / Demo ----------------
    def _refresh_ports(self):
        self.port_combo.clear()
        for p in serial.tools.list_ports.comports():
            self.port_combo.addItem(f"{p.device}  {p.description}")
        if self.port_combo.count() == 0:
            self.port_combo.addItem("（未发现串口，请手动输入）")

    def _toggle_connect(self):
        if self._connected:
            self.worker.close()
            return
        text = self.port_combo.currentText().strip()
        if not text or text.startswith("（"):
            self._on_log("请先选择或输入串口号")
            return
        port = text.split()[0]
        try:
            baud = int(self.baud_combo.currentText())
        except ValueError:
            baud = 115200
        # 直接调用（串口打开很快）；不用 invokeMethod，后者要求 @pyqtSlot 否则静默失败
        self.worker.open(port, baud)

    def _toggle_demo(self, on: bool):
        if on:
            self._apply_widgets_to_cfg()
            self.demo.set_config(self.cfg)
            self.demo.reset()
            self.demo.start()
            self._demo_on = True
            self.demo_btn.setText("停止 Demo 模式")
            self._on_log("Demo 模式已启动（本地生成数据）")
        else:
            self.demo.stop()
            self._demo_on = False
            self.demo_btn.setText("启动 Demo 模式")
            self._on_log("Demo 模式已停止")

    # ---------------- 命令 ----------------
    def _send_frame(self, ftype, payload=b""):
        seq = self._seq; self._seq = (self._seq + 1) & 0xFFFF
        ts = int(time.time() * 1000) & 0xFFFFFFFF
        frame = P.build_frame(ftype, seq, ts, payload)
        self.worker.send(frame)

    def _apply_widgets_to_cfg(self):
        self.cfg.sample_rate_hz = self.in_rate.value()
        self.cfg.n_targets = self.in_ntgt.value()
        self.cfg.motion_model = self.in_motion.currentData()
        self.cfg.motion_period_ms = self.in_period.value()
        self.cfg.toa_base_ns = self.in_toa_base.value()
        self.cfg.toa_span_ns = self.in_toa_span.value()
        self.cfg.aoa_center_deg_x10 = self.in_aoa_ctr.value()
        self.cfg.aoa_span_deg_x10 = self.in_aoa_span.value()
        self.cfg.tdoa_center_ns_x10 = self.in_tdoa_ctr.value()
        self.cfg.tdoa_span_ns_x10 = self.in_tdoa_span.value()
        self.cfg.noise_sigma_x10 = int(round(self.in_noise.value() * 10))
        self.cfg.telemetry_decim = self.in_decim.value()

    def _sync_cfg_to_widgets(self):
        self.in_rate.setValue(self.cfg.sample_rate_hz)
        self.in_ntgt.setValue(self.cfg.n_targets)
        self.in_motion.setCurrentIndex(self.cfg.motion_model)
        self.in_period.setValue(self.cfg.motion_period_ms)
        self.in_toa_base.setValue(self.cfg.toa_base_ns)
        self.in_toa_span.setValue(self.cfg.toa_span_ns)
        self.in_aoa_ctr.setValue(self.cfg.aoa_center_deg_x10)
        self.in_aoa_span.setValue(self.cfg.aoa_span_deg_x10)
        self.in_tdoa_ctr.setValue(self.cfg.tdoa_center_ns_x10)
        self.in_tdoa_span.setValue(self.cfg.tdoa_span_ns_x10)
        self.in_noise.setValue(self.cfg.noise_sigma_x10 / 10.0)
        self.in_decim.setValue(self.cfg.telemetry_decim)

    def _send_config(self):
        self._apply_widgets_to_cfg()
        if self._demo_on:
            self.demo.set_config(self.cfg)
            self._on_log("Demo 配置已更新")
        self._send_frame(P.CMD_SET_CONFIG, self.cfg.pack())
        self._on_log(f"已下发配置：{self.cfg.n_targets}目标 {self.cfg.sample_rate_hz}Hz "
                     f"模型{self.cfg.motion_model}")

    def _cmd_start(self):
        if self._demo_on:
            self._on_log("Demo 模式下无需 Start")
            return
        self._send_frame(P.CMD_START)
    def _cmd_stop(self):
        self._send_frame(P.CMD_STOP)
    def _cmd_ping(self):
        self._send_frame(P.CMD_PING)
    def _cmd_reset(self):
        self._send_frame(P.CMD_RESET_SEQ)
        self._frame_count = 0
    def _cmd_clear(self):
        """清空所有绘图缓冲与表格，不影响运行状态"""
        self._t_buf.clear()
        for i in range(8):
            self._toa[i].clear(); self._az[i].clear(); self._el[i].clear()
            self._tdoa[i].clear(); self._posx[i].clear(); self._posy[i].clear()
            self._cur_pos[i] = [0, 0]
        for r in range(self.table.rowCount()):
            for c in range(self.table.columnCount()):
                self.table.item(r, c).setText("")
        self._refresh_plots()
        self._on_log("已清空绘图缓冲")
    def _cmd_status(self):
        self._send_frame(P.CMD_GET_STATUS)

    def closeEvent(self, e):
        self.demo.stop()
        self.worker._stop = True          # 让 run 循环退出
        try:
            self.worker.close()
        except Exception:
            pass
        self.thread.quit()
        self.thread.wait(2000)
        super().closeEvent(e)


def main():
    app = QtWidgets.QApplication(sys.argv)
    w = MainWindow()
    w.show()
    sys.exit(app.exec_())


if __name__ == "__main__":
    main()
