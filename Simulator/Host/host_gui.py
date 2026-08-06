"""
host_gui.py - F407 仿真传感器上位机 (RK3568 SPI 接收端配套)

F407 作 SPI 从机向 RK3568 发送 182B SpiFrame (每锚点 TOA/TDOA/AOA/RSS)。
本上位机经 UART 控制 F407 并接收:
  - TRUTH 帧 (每帧, 24B): 目标真实位置/速度 -> 位置图丝滑绘图
  - DATA 帧 (抽稀, 182B SpiFrame): 每锚点测量 -> 测量曲线/表格
  - STATUS 帧: F407 运行状态

Demo 模式: 本地用与固件一致的 DemoSim 生成数据, 无硬件可独立演示。

依赖: PyQt5, pyqtgraph, pyserial   运行: python host_gui.py
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

PLOT_WINDOW = 1000
PLOT_RATE_HZ = 30
POS_TRAIL = 400
ANCHOR_COLORS = ["#e74c3c", "#27ae60", "#2980b9", "#8e44ad", "#f39c12"]


# ============================================================
#  本地仿真源 (复刻固件 sim.c)
# ============================================================
class DemoWorker(QtCore.QObject):
    frame_ready = QtCore.pyqtSignal(int, int, int, bytes)   # type, seq, ts, payload

    def __init__(self, cfg: P.Config):
        super().__init__()
        self.cfg = cfg
        self.sim = P.DemoSim(cfg)
        self._seq = 0
        self._skip = 0
        self._t0 = time.monotonic()
        self._timer = QtCore.QTimer(self)
        self._timer.timeout.connect(self._tick)

    def set_config(self, cfg: P.Config):
        self.cfg = cfg
        self.sim.apply(cfg)
        if self._timer.isActive():
            self.stop(); self.start()

    def start(self):
        self.sim.reset()
        self._t0 = time.monotonic()
        self._timer.start(int(1000 / max(self.cfg.sample_rate_hz, 1)))

    def stop(self):
        self._timer.stop()

    def _tick(self):
        now_us = int((time.monotonic() - self._t0) * 1e6)
        fb, tr = self.sim.step(now_us)
        ts = int((time.monotonic() - self._t0) * 1000) & 0xFFFFFFFF
        # TRUTH 每帧
        tp = struct.pack(P.TRUTH_FMT, tr.x, tr.y, tr.z, tr.vx, tr.vy, tr.vz)
        s = self._seq; self._seq = (self._seq + 1) & 0xFFFF
        self.frame_ready.emit(P.RSP_TRUTH, s, ts, tp)
        # SpiFrame 抽稀
        if self.cfg.telemetry_decim > 0:
            if self._skip == 0:
                self.frame_ready.emit(P.RSP_DATA_FRAME, s, ts, fb)
                self._skip = self.cfg.telemetry_decim - 1
            else:
                self._skip -= 1


# ============================================================
#  串口读线程
# ============================================================
class SerialWorker(QtCore.QObject):
    frame_ready = QtCore.pyqtSignal(int, int, int, bytes)
    crc_error = QtCore.pyqtSignal()
    log = QtCore.pyqtSignal(str)
    connection_changed = QtCore.pyqtSignal(bool)

    def __init__(self):
        super().__init__()
        self.ser = None
        self._stop = False
        self._parser = P.FrameParser()
        self._lock = QtCore.QMutex()

    def open(self, port, baud):
        try:
            self.ser = serial.Serial(port, baud, timeout=0.05, write_timeout=0.2)
            self.connection_changed.emit(True)
            self.log.emit(f"已连接 {port} @ {baud}")
            return True
        except Exception as e:
            self.ser = None
            self.connection_changed.emit(False)
            self.log.emit(f"连接失败: {e}")
            return False

    def close(self):
        s = self.ser; self.ser = None
        if s:
            try: s.close()
            except Exception: pass
            self.connection_changed.emit(False)
            self.log.emit("已断开")

    def send(self, data: bytes):
        s = self.ser
        if not s: return
        self._lock.lock()
        try: s.write(data)
        except Exception as e: self.log.emit(f"发送失败: {e}")
        finally: self._lock.unlock()

    def run(self):
        while not self._stop:
            s = self.ser
            if s is None:
                QtCore.QThread.msleep(50); continue
            try:
                data = s.read(2048)
            except Exception as e:
                self.log.emit(f"读取异常: {e}"); QtCore.QThread.msleep(100); continue
            if not data: continue
            for item in self._parser.feed(data):
                if item[0] == "CRC_ERROR": self.crc_error.emit()
                else: self.frame_ready.emit(*item)


# ============================================================
#  主窗口
# ============================================================
class MainWindow(QtWidgets.QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("F407 仿真传感器上位机 - SPI Slave (RK3568 接收端)")
        self.resize(1320, 860)

        self.cfg = P.Config()
        self.demo = DemoWorker(self.cfg)
        self.demo.frame_ready.connect(self._on_frame)
        self.worker = SerialWorker()
        self.worker.frame_ready.connect(self._on_frame)
        self.worker.crc_error.connect(self._on_crc_error)
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
        self._rx_count = 0
        self._rx_t0 = time.monotonic()
        self._rx_fps = 0.0

        # 位置轨迹 (来自 TRUTH)
        self._posx = deque(maxlen=POS_TRAIL)
        self._posy = deque(maxlen=POS_TRAIL)
        self._cur_xy = (0.0, 0.0)
        # 每锚点测量时序 (来自 SpiFrame DATA)
        self._t_buf = deque(maxlen=PLOT_WINDOW)
        self._toa = [deque(maxlen=PLOT_WINDOW) for _ in range(P.N_ANC)]
        self._az = [deque(maxlen=PLOT_WINDOW) for _ in range(P.N_ANC)]
        self._rss = [deque(maxlen=PLOT_WINDOW) for _ in range(P.N_ANC)]

        self._build_ui()
        self._sync_cfg_to_widgets()

        self._plot_timer = QtCore.QTimer(self)
        self._plot_timer.timeout.connect(self._refresh_plots)
        self._plot_timer.start(int(1000 / PLOT_RATE_HZ))

    # ---------------- UI ----------------
    def _build_ui(self):
        central = QtWidgets.QWidget(); self.setCentralWidget(central)
        root = QtWidgets.QHBoxLayout(central)
        left = QtWidgets.QFrame(); left.setFrameShape(QtWidgets.QFrame.StyledPanel)
        left.setMinimumWidth(340); left.setMaximumWidth(370)
        ll = QtWidgets.QVBoxLayout(left)
        root.addWidget(left)
        root.addWidget(self._build_plots(), 1)
        root.addWidget(self._build_right(), 0)

        self._build_connection_box(ll)
        self._build_config_box(ll)
        self._build_control_box(ll)
        ll.addStretch(1)
        self._build_status_box(ll)

    def _build_connection_box(self, ll):
        g = QtWidgets.QGroupBox("连接 Connection"); gl = QtWidgets.QGridLayout(g)
        self.port_combo = QtWidgets.QComboBox(); self.port_combo.setEditable(True)
        self.refresh_btn = QtWidgets.QPushButton("刷新"); self.refresh_btn.clicked.connect(self._refresh_ports)
        gl.addWidget(QtWidgets.QLabel("端口"), 0, 0); gl.addWidget(self.port_combo, 0, 1); gl.addWidget(self.refresh_btn, 0, 2)
        self.baud_combo = QtWidgets.QComboBox(); self.baud_combo.addItems(["115200","230400","460800","921600"])
        gl.addWidget(QtWidgets.QLabel("波特"), 1, 0); gl.addWidget(self.baud_combo, 1, 1, 1, 2)
        self.connect_btn = QtWidgets.QPushButton("连接 Connect"); self.connect_btn.clicked.connect(self._toggle_connect)
        self.demo_btn = QtWidgets.QPushButton("启动 Demo"); self.demo_btn.setCheckable(True); self.demo_btn.toggled.connect(self._toggle_demo)
        hb = QtWidgets.QHBoxLayout(); hb.addWidget(self.connect_btn); hb.addWidget(self.demo_btn)
        gl.addLayout(hb, 2, 0, 1, 3)
        ll.addWidget(g); self._refresh_ports()

    def _build_config_box(self, ll):
        g = QtWidgets.QGroupBox("仿真配置 Config"); f = QtWidgets.QFormLayout(g)
        self.in_rate = QtWidgets.QSpinBox(); self.in_rate.setRange(1, 1000); self.in_rate.setValue(100)
        self.in_scene = QtWidgets.QComboBox()
        for k, v in P.SCENE_NAMES.items(): self.in_scene.addItem(v, k)
        self.in_decim = QtWidgets.QSpinBox(); self.in_decim.setRange(0, 255); self.in_decim.setValue(10)
        # 模态复选
        mod = QtWidgets.QGroupBox("模态 Modality"); mgl = QtWidgets.QGridLayout(mod)
        self.chk_tdoa = QtWidgets.QCheckBox("TDOA"); self.chk_tdoa.setChecked(True)
        self.chk_toa  = QtWidgets.QCheckBox("TOA");  self.chk_toa.setChecked(True)
        self.chk_aoa  = QtWidgets.QCheckBox("AOA");  self.chk_aoa.setChecked(True)
        self.chk_rss  = QtWidgets.QCheckBox("RSS");  self.chk_rss.setChecked(True)
        mgl.addWidget(self.chk_tdoa,0,0); mgl.addWidget(self.chk_toa,0,1); mgl.addWidget(self.chk_aoa,1,0); mgl.addWidget(self.chk_rss,1,1)
        # 自定义初值/速度
        self.in_ix = QtWidgets.QDoubleSpinBox(); self.in_iy = QtWidgets.QDoubleSpinBox(); self.in_iz = QtWidgets.QDoubleSpinBox()
        self.in_vx = QtWidgets.QDoubleSpinBox(); self.in_vy = QtWidgets.QDoubleSpinBox(); self.in_vz = QtWidgets.QDoubleSpinBox()
        for w in (self.in_ix,self.in_iy,self.in_iz,self.in_vx,self.in_vy,self.in_vz):
            w.setRange(-999, 999); w.setDecimals(2); w.setSingleStep(0.5)
        self.in_ix.setValue(8); self.in_iy.setValue(10); self.in_iz.setValue(6)
        self.in_vx.setValue(1.2); self.in_vy.setValue(0.75); self.in_vz.setValue(0.2)
        f.addRow("采样率 (Hz)", self.in_rate)
        f.addRow("场景 Scene", self.in_scene)
        f.addRow(mod)
        f.addRow("遥测抽稀", self.in_decim)
        f.addRow("初值 X/Y/Z", self._hbox(self.in_ix, self.in_iy, self.in_iz))
        f.addRow("速度 Vx/Vy/Vz", self._hbox(self.in_vx, self.in_vy, self.in_vz))
        self.apply_cfg_btn = QtWidgets.QPushButton("下发配置 Send Config"); self.apply_cfg_btn.clicked.connect(self._send_config)
        f.addRow(self.apply_cfg_btn)
        ll.addWidget(g)

    @staticmethod
    def _hbox(*widgets):
        h = QtWidgets.QHBoxLayout()
        for w in widgets: h.addWidget(w)
        return h

    def _build_control_box(self, ll):
        g = QtWidgets.QGroupBox("控制 Control"); gl = QtWidgets.QGridLayout(g)
        self.start_btn = QtWidgets.QPushButton("▶ Start")
        self.stop_btn = QtWidgets.QPushButton("■ Stop")
        self.clear_btn = QtWidgets.QPushButton("🧹 清空")
        self.ping_btn = QtWidgets.QPushButton("Ping")
        self.reset_btn = QtWidgets.QPushButton("Reset")
        self.status_btn = QtWidgets.QPushButton("Get Status")
        for b, fn in [(self.start_btn,self._cmd_start),(self.stop_btn,self._cmd_stop),
                      (self.clear_btn,self._cmd_clear),(self.ping_btn,self._cmd_ping),
                      (self.reset_btn,self._cmd_reset),(self.status_btn,self._cmd_status)]:
            b.clicked.connect(fn)
        gl.addWidget(self.start_btn,0,0); gl.addWidget(self.stop_btn,0,1); gl.addWidget(self.clear_btn,0,2)
        gl.addWidget(self.ping_btn,1,0); gl.addWidget(self.reset_btn,1,1); gl.addWidget(self.status_btn,1,2)
        ll.addWidget(g)

    def _build_status_box(self, ll):
        g = QtWidgets.QGroupBox("状态 Status"); f = QtWidgets.QFormLayout(g)
        self.lbl_conn = QtWidgets.QLabel("● 离线"); self.lbl_conn.setStyleSheet("color:#888")
        self.lbl_frames = QtWidgets.QLabel("0"); self.lbl_rate = QtWidgets.QLabel("-")
        self.lbl_crc = QtWidgets.QLabel("0"); self.lbl_remote = QtWidgets.QLabel("-")
        f.addRow("链路", self.lbl_conn); f.addRow("接收帧数", self.lbl_frames)
        f.addRow("实时速率", self.lbl_rate); f.addRow("CRC 错误", self.lbl_crc)
        f.addRow("远端状态", self.lbl_remote)
        ll.addWidget(g)

    def _build_plots(self):
        w = QtWidgets.QWidget(); h = QtWidgets.QHBoxLayout(w); h.setSpacing(4)
        pg.setConfigOptions(antialias=True)
        # 左：位置图 (大)
        self.plot_pos = pg.PlotWidget(title="实时位置 Position (真值) + 锚点 Anchors")
        self.plot_pos.showGrid(x=True, y=True, alpha=0.3)
        self.plot_pos.setLabel("bottom","X",units="m"); self.plot_pos.setLabel("left","Y",units="m")
        self.plot_pos.setAspectLocked(True)
        self.curve_trail = self.plot_pos.plot(pen=pg.mkPen("#2980b9", width=2))
        self.target_dot = pg.ScatterPlotItem(size=14, symbol="o",
                            brush=pg.mkBrush("#2980b9"), pen=pg.mkPen("#111", width=1.5))
        self.plot_pos.addItem(self.target_dot)
        self.bs_scatter = pg.ScatterPlotItem(size=16, symbol="t1",
                            brush=pg.mkBrush("#e74c3c"), pen=pg.mkPen("#7f0f16", width=1.5))
        self.plot_pos.addItem(self.bs_scatter)
        self.bs_labels = []
        for i,(x,y,z) in enumerate(P.ANCHORS):
            t = pg.TextItem(f"A{i}", color="#c0392b", anchor=(0.5,-0.6)); t.setPos(x,y)
            self.plot_pos.addItem(t); self.bs_labels.append(t)
        # 右：三幅时序
        right = QtWidgets.QWidget(); rv = QtWidgets.QVBoxLayout(right)
        self.plot_toa = pg.PlotWidget(title="TOA (s) per anchor")
        self.plot_az = pg.PlotWidget(title="AOA 方位 (rad) per anchor")
        self.plot_rss = pg.PlotWidget(title="RSS (dBm) per anchor")
        for p in (self.plot_toa, self.plot_az, self.plot_rss):
            p.showGrid(x=True, y=True, alpha=0.3); p.setLabel("bottom","时间",units="s")
            p.addLegend(offset=(10,10))
            rv.addWidget(p)
        self.curves_toa = []; self.curves_az = []; self.curves_rss = []
        for i in range(P.N_ANC):
            c = ANCHOR_COLORS[i]
            self.curves_toa.append(self.plot_toa.plot(pen=pg.mkPen(c,width=1.5), name=f"A{i}"))
            self.curves_az.append(self.plot_az.plot(pen=pg.mkPen(c,width=1.5), name=f"A{i}"))
            self.curves_rss.append(self.plot_rss.plot(pen=pg.mkPen(c,width=1.5), name=f"A{i}"))
        h.addWidget(self.plot_pos, 1)
        h.addWidget(right, 1)
        return w

    def _build_right(self):
        w = QtWidgets.QWidget(); w.setMaximumWidth(440); l = QtWidgets.QVBoxLayout(w)
        l.addWidget(QtWidgets.QLabel("每锚点测量 Latest SpiFrame"))
        self.table = QtWidgets.QTableWidget(P.N_ANC, 6)
        self.table.setHorizontalHeaderLabels(["anc","has","TOA(s)","AOA_az(rad)","AOA_el(rad)","RSS(dBm)"])
        self.table.verticalHeader().setVisible(False)
        for r in range(P.N_ANC):
            for c in range(6): self.table.setItem(r, c, QtWidgets.QTableWidgetItem(""))
        self.table.horizontalHeader().setSectionResizeMode(QtWidgets.QHeaderView.Stretch)
        l.addWidget(self.table)
        l.addWidget(QtWidgets.QLabel("日志 / Log"))
        self.log_view = QtWidgets.QPlainTextEdit(); self.log_view.setReadOnly(True); self.log_view.setMaximumBlockCount(2000)
        l.addWidget(self.log_view, 1)
        return w

    # ---------------- 帧处理 ----------------
    def _on_frame(self, ftype, seq, ts, payload):
        if ftype == P.RSP_TRUTH:
            self._rx_count += 1
            try: tr = P.parse_truth(payload)
            except Exception: return
            self._posx.append(tr.x); self._posy.append(tr.y); self._cur_xy = (tr.x, tr.y)
        elif ftype == P.RSP_DATA_FRAME:
            self._frame_count += 1
            try: sf = P.parse_spi_frame(payload)
            except Exception as e: self._on_log(f"SpiFrame 解析: {e}"); return
            self._on_spi_frame(sf)
        elif ftype == P.RSP_PONG:
            self._on_log(f"PONG seq={seq}: {payload!r}")
        elif ftype == P.RSP_ACK:
            if len(payload) >= 2: self._on_log(f"ACK cmd=0x{payload[0]:02X} stat=0x{payload[1]:02X}")
        elif ftype == P.RSP_NACK:
            if len(payload) >= 2: self._on_log(f"NACK cmd=0x{payload[0]:02X} stat=0x{payload[1]:02X}")
        elif ftype == P.RSP_STATUS:
            try:
                st = P.Status.unpack(payload)
                self.lbl_remote.setText(f"{'运行' if st.running else '停止'} | 场景{st.scene} | "
                    f"{st.sample_rate_hz}Hz | 帧{st.frame_count} | SPI读{st.spi_xfer} | "
                    f"UART溢{st.uart_overrun} | CRC{st.crc_errors} | cmd{st.cmd_count}")
                self._on_log(f"← STATUS seq={seq} 运行={st.running} 帧={st.frame_count} "
                             f"SPI读={st.spi_xfer} CRC={st.crc_errors} (len={len(payload)})")
            except Exception as e: self._on_log(f"STATUS 解析失败: {e} (len={len(payload)})")
        elif ftype == P.RSP_LOG:
            self._on_log(f"[LOG] {payload.decode('utf-8','replace')}")

    def _on_spi_frame(self, sf: P.SpiFrame):
        t = (sf.seq % 100000) * 0.01   # 近似时间轴 (避免依赖 dt)
        self._t_buf.append(t)
        for i in range(P.N_ANC):
            a = sf.anchors[i]
            self._toa[i].append(a.toa_sec if (a.has & P.SPI_MODE_TOA) else float('nan'))
            self._az[i].append(a.aoa_az if (a.has & P.SPI_MODE_AOA) else float('nan'))
            self._rss[i].append(a.rss_dbm if (a.has & P.SPI_MODE_RSS) else float('nan'))
            if i < self.table.rowCount():
                self.table.item(i,0).setText(f"A{i}")
                self.table.item(i,1).setText(f"0x{a.has:01x}")
                self.table.item(i,2).setText(f"{a.toa_sec:.3e}" if (a.has & P.SPI_MODE_TOA) else "-")
                self.table.item(i,3).setText(f"{a.aoa_az:.3f}" if (a.has & P.SPI_MODE_AOA) else "-")
                self.table.item(i,4).setText(f"{a.aoa_el:.3f}" if (a.has & P.SPI_MODE_AOA) else "-")
                self.table.item(i,5).setText(f"{a.rss_dbm:.1f}" if (a.has & P.SPI_MODE_RSS) else "-")

    def _on_crc_error(self): self._crc_errors += 1
    def _on_log(self, msg): self.log_view.appendPlainText(f"[{time.strftime('%H:%M:%S')}] {msg}")

    def _on_conn_changed(self, ok):
        self._connected = ok
        self.lbl_conn.setText("● 已连接" if ok else "● 离线")
        self.lbl_conn.setStyleSheet("color:#27ae60" if ok else "color:#888")
        self.connect_btn.setText("断开" if ok else "连接 Connect")
        if ok: self._send_frame(P.CMD_PING)

    # ---------------- 刷新 ----------------
    def _refresh_plots(self):
        self.lbl_frames.setText(str(self._frame_count))
        self.lbl_crc.setText(str(self._crc_errors))
        now = time.monotonic(); dt = now - self._rx_t0
        if dt >= 0.5:
            self._rx_fps = self._rx_count / dt; self._rx_count = 0; self._rx_t0 = now
        self.lbl_rate.setText(f"{self._rx_fps:.1f} Hz")
        # 位置
        self.curve_trail.setData(list(self._posx), list(self._posy))
        if self._posx:
            self.target_dot.setData([{"pos": self._cur_xy, "size": 14,
                                       "brush": pg.mkBrush("#2980b9"), "pen": pg.mkPen("#111", width=1.5)}])
        # 时序
        xs = list(self._t_buf)
        for i in range(P.N_ANC):
            self.curves_toa[i].setData(xs, list(self._toa[i]), connect="finite")
            self.curves_az[i].setData(xs, list(self._az[i]), connect="finite")
            self.curves_rss[i].setData(xs, list(self._rss[i]), connect="finite")

    # ---------------- 串口 / Demo ----------------
    def _refresh_ports(self):
        self.port_combo.clear()
        for p in serial.tools.list_ports.comports():
            self.port_combo.addItem(f"{p.device}  {p.description}")
        if self.port_combo.count() == 0:
            self.port_combo.addItem("（未发现串口，请手动输入）")

    def _toggle_connect(self):
        if self._connected:
            self.worker.close(); return
        text = self.port_combo.currentText().strip()
        if not text or text.startswith("（"): self._on_log("请先选择或输入串口号"); return
        port = text.split()[0]
        try: baud = int(self.baud_combo.currentText())
        except ValueError: baud = 115200
        self.worker.open(port, baud)

    def _toggle_demo(self, on):
        if on:
            self._apply_widgets_to_cfg()
            self.demo.set_config(self.cfg); self.demo.start()
            self._demo_on = True; self.demo_btn.setText("停止 Demo")
            self._on_log("Demo 模式启动")
        else:
            self.demo.stop(); self._demo_on = False; self.demo_btn.setText("启动 Demo")
            self._on_log("Demo 模式停止")

    # ---------------- 命令 ----------------
    def _send_frame(self, ftype, payload=b""):
        if not self._connected and not self._demo_on:
            self._on_log("未连接串口，命令未发送（请先连接，或用 Demo 模式）")
            return
        seq = self._seq; self._seq = (self._seq + 1) & 0xFFFF
        ts = int(time.time() * 1000) & 0xFFFFFFFF
        self.worker.send(P.build_frame(ftype, seq, ts, payload))

    def _apply_widgets_to_cfg(self):
        self.cfg.sample_rate_hz = self.in_rate.value()
        self.cfg.scene = self.in_scene.currentData()
        m = 0
        if self.chk_tdoa.isChecked(): m |= P.SPI_MODE_TDOA
        if self.chk_toa.isChecked():  m |= P.SPI_MODE_TOA
        if self.chk_aoa.isChecked():  m |= P.SPI_MODE_AOA
        if self.chk_rss.isChecked():  m |= P.SPI_MODE_RSS
        self.cfg.enable_mask = m if m else (P.SPI_MODE_TDOA|P.SPI_MODE_TOA|P.SPI_MODE_AOA|P.SPI_MODE_RSS)
        self.cfg.telemetry_decim = self.in_decim.value()
        self.cfg.init_x = self.in_ix.value(); self.cfg.init_y = self.in_iy.value(); self.cfg.init_z = self.in_iz.value()
        self.cfg.vel_x = self.in_vx.value(); self.cfg.vel_y = self.in_vy.value(); self.cfg.vel_z = self.in_vz.value()

    def _sync_cfg_to_widgets(self):
        self.in_rate.setValue(self.cfg.sample_rate_hz)
        self.in_scene.setCurrentIndex(self.cfg.scene)
        self.chk_tdoa.setChecked(bool(self.cfg.enable_mask & P.SPI_MODE_TDOA))
        self.chk_toa.setChecked(bool(self.cfg.enable_mask & P.SPI_MODE_TOA))
        self.chk_aoa.setChecked(bool(self.cfg.enable_mask & P.SPI_MODE_AOA))
        self.chk_rss.setChecked(bool(self.cfg.enable_mask & P.SPI_MODE_RSS))
        self.in_decim.setValue(self.cfg.telemetry_decim)
        self.in_ix.setValue(self.cfg.init_x); self.in_iy.setValue(self.cfg.init_y); self.in_iz.setValue(self.cfg.init_z)
        self.in_vx.setValue(self.cfg.vel_x); self.in_vy.setValue(self.cfg.vel_y); self.in_vz.setValue(self.cfg.vel_z)

    def _send_config(self):
        self._apply_widgets_to_cfg()
        if self._demo_on:
            self.demo.set_config(self.cfg); self._on_log("Demo 配置已更新")
        self._send_frame(P.CMD_SET_CONFIG, self.cfg.pack())
        self._on_log(f"已下发: 场景{P.SCENE_NAMES[self.cfg.scene]} {self.cfg.sample_rate_hz}Hz 模态0x{self.cfg.enable_mask:x}")

    def _cmd_start(self):
        if self._demo_on: self._on_log("Demo 模式无需 Start"); return
        self._send_frame(P.CMD_START)
    def _cmd_stop(self): self._send_frame(P.CMD_STOP)
    def _cmd_ping(self): self._send_frame(P.CMD_PING)
    def _cmd_reset(self): self._send_frame(P.CMD_RESET_SEQ); self._frame_count = 0
    def _cmd_status(self):
        self._on_log("-> 发送 Get Status")
        self._send_frame(P.CMD_GET_STATUS)
    def _cmd_clear(self):
        self._posx.clear(); self._posy.clear(); self._t_buf.clear()
        for i in range(P.N_ANC):
            self._toa[i].clear(); self._az[i].clear(); self._rss[i].clear()
        for r in range(self.table.rowCount()):
            for c in range(self.table.columnCount()): self.table.item(r,c).setText("")
        self._refresh_plots(); self._on_log("已清空")

    def closeEvent(self, e):
        self.demo.stop(); self.worker._stop = True
        try: self.worker.close()
        except Exception: pass
        self.thread.quit(); self.thread.wait(2000)
        super().closeEvent(e)


def main():
    app = QtWidgets.QApplication(sys.argv)
    w = MainWindow(); w.show()
    sys.exit(app.exec_())


if __name__ == "__main__":
    main()
