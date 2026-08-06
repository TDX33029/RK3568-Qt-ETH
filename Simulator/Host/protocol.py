"""
protocol.py - 与 F407 固件共用的协议

UART 帧定界（与之前一致）:
  SYNC0 SYNC1 | TYPE | LEN(2) | SEQ(2) | TS(4) | PAYLOAD | CRC16(2)

SPI 链路（F407 从机 -> RK3568 主机）: 固定 182 字节 SpiFrame，与 RK3568 端
spi_protocol.h 逐字节一致。UART 的 DATA_FRAME 载荷 = 整个 SpiFrame。
"""
from __future__ import annotations
import struct
from dataclasses import dataclass, field

# ---- UART 帧结构 ----
SYNC0 = 0xA5
SYNC1 = 0x5A
HDR_LEN = 9
CRC_LEN = 2
MAX_PAYLOAD = 256
MAX_FRAME_LEN = 2 + HDR_LEN + CRC_LEN + MAX_PAYLOAD

# ---- 帧类型 ----
CMD_PING       = 0x01
CMD_SET_CONFIG = 0x02
CMD_START      = 0x03
CMD_STOP       = 0x04
CMD_RESET_SEQ  = 0x05
CMD_SET_RATE   = 0x06
CMD_GET_STATUS = 0x07
RSP_PONG       = 0x81
RSP_ACK        = 0x82
RSP_NACK       = 0x83
RSP_STATUS     = 0x84
RSP_DATA_FRAME = 0x85   # 载荷 = SpiFrame (182B)
RSP_LOG        = 0x86
RSP_TRUTH      = 0x87   # 载荷 = 真实位置/速度 (24B)

STAT_OK               = 0x00
STAT_ERR_LEN         = 0x01
STAT_ERR_CRC         = 0x02
STAT_ERR_PARAM       = 0x03
STAT_ERR_BUSY        = 0x04
STAT_ERR_NOT_RUNNING = 0x05
STAT_ERR_UNSUPPORTED = 0x06

# ---- SPI 帧 (182B) ----
SPI_MAGIC0 = 0xA5
SPI_MAGIC1 = 0x5A
SPI_MAX_ANCHORS = 8
SPI_FRAME_LEN = 182
SPI_ANCHOR_LEN = 21

SPI_MODE_TDOA = 1 << 0
SPI_MODE_TOA  = 1 << 1
SPI_MODE_AOA  = 1 << 2
SPI_MODE_RSS  = 1 << 3

# ---- 场景 ----
SCENE_STRAIGHT = 0
SCENE_CLIMB    = 1
SCENE_TURN     = 2
SCENE_CUSTOM   = 3
SCENE_NAMES = {0: "直线 Straight", 1: "爬升 Climb", 2: "转弯 Turn", 3: "自定义 Custom"}

# ---- 锚点坐标 (与 RK configure_demo_anchors 一致) ----
ANCHORS = [
    (0.0,  0.0,  0.0),   # A0 ref
    (30.0, 0.0,  4.0),
    (30.0, 30.0, 0.0),
    (0.0,  30.0, 5.0),
    (15.0, 15.0, 12.0),
]
N_ANC = 5
REF_ANC = 0

# ---- 物理常量 (与固件一致) ----
C_LIGHT = 299792458.0
TDOA_STD = 2.0e-9
TOA_STD = 2.0e-9
AOA_STD = 1.0 * 3.14159265358979 / 180.0
RSS_STD = 2.0
RSS_REF = -35.0
RSS_N = 2.0

# ---- 结构体格式 (与固件 packed 一致) ----
CONFIG_FMT = "<IBBBBffffff"      # 32B
STATUS_FMT = "<BBBBIIIIII"       # 28B
TRUTH_FMT  = "<ffffff"           # 24B
ANCHOR_FMT = "<Bfffff"           # 21B
SPI_HDR_FMT = "<HBBIH"          # seq(2)+mode_mask(1)+n_anc(1)+dt_us(4)+reserved(2) = 10B (after magic)

CONFIG_SIZE = struct.calcsize(CONFIG_FMT)
STATUS_SIZE = struct.calcsize(STATUS_FMT)
TRUTH_SIZE = struct.calcsize(TRUTH_FMT)
ANCHOR_SIZE = struct.calcsize(ANCHOR_FMT)
SPI_HDR_SIZE = struct.calcsize(SPI_HDR_FMT)   # 10 (after magic)
SPI_ANC_OFF = 12   # anchors start at byte 12 (after magic+seq+mode_mask+n_anc+dt_us+reserved)


def crc16(data: bytes) -> int:
    """CRC-16/CCITT-FALSE (与固件 + RK 一致)"""
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def build_frame(type_: int, seq: int, ts_ms: int, payload: bytes = b"") -> bytes:
    if len(payload) > MAX_PAYLOAD:
        raise ValueError("payload too large")
    head = struct.pack("<BBBHHI", SYNC0, SYNC1, type_ & 0xFF,
                       len(payload) & 0xFFFF, seq & 0xFFFF, ts_ms & 0xFFFFFFFF)
    body = head[2:] + payload
    crc = struct.pack("<H", crc16(body))
    return head + payload + crc


@dataclass
class Config:
    sample_rate_hz: int = 100
    scene: int = SCENE_STRAIGHT
    enable_mask: int = SPI_MODE_TDOA | SPI_MODE_TOA | SPI_MODE_AOA | SPI_MODE_RSS
    telemetry_decim: int = 10
    reserved: int = 0
    init_x: float = 8.0
    init_y: float = 10.0
    init_z: float = 6.0
    vel_x: float = 1.20
    vel_y: float = 0.75
    vel_z: float = 0.20

    def pack(self) -> bytes:
        return struct.pack(CONFIG_FMT, self.sample_rate_hz & 0xFFFFFFFF,
                           self.scene & 0xFF, self.enable_mask & 0xFF,
                           self.telemetry_decim & 0xFF, self.reserved & 0xFF,
                           self.init_x, self.init_y, self.init_z,
                           self.vel_x, self.vel_y, self.vel_z)

    @classmethod
    def unpack(cls, data: bytes) -> "Config":
        f = struct.unpack(CONFIG_FMT, data)
        return cls(f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7], f[8], f[9], f[10])


@dataclass
class Anchor:
    has: int
    tdoa_sec: float
    toa_sec: float
    aoa_az: float
    aoa_el: float
    rss_dbm: float


@dataclass
class SpiFrame:
    seq: int
    mode_mask: int
    n_anc: int
    dt_us: int
    anchors: list  # list[Anchor]


@dataclass
class Truth:
    x: float; y: float; z: float
    vx: float; vy: float; vz: float


@dataclass
class Status:
    running: int; scene: int; enable_mask: int
    sample_rate_hz: int; frame_count: int; spi_xfer: int
    uart_overrun: int; crc_errors: int; cmd_count: int

    @classmethod
    def unpack(cls, data: bytes) -> "Status":
        r, sc, em, _res, sr, fc, sx, uo, ce, cc = struct.unpack(STATUS_FMT, data)
        return cls(r, sc, em, sr, fc, sx, uo, ce, cc)


def parse_spi_frame(payload: bytes) -> SpiFrame:
    """从 182B 解析 SpiFrame；校验 magic + CRC"""
    if len(payload) != SPI_FRAME_LEN:
        raise ValueError(f" SpiFrame len {len(payload)} != {SPI_FRAME_LEN}")
    if payload[0] != SPI_MAGIC0 or payload[1] != SPI_MAGIC1:
        raise ValueError("magic mismatch")
    crc_recv = payload[180] | (payload[181] << 8)
    crc_calc = crc16(payload[:180])
    if crc_recv != crc_calc:
        raise ValueError(f"CRC mismatch {crc_recv:#x} vs {crc_calc:#x}")
    seq, mode_mask, n_anc, dt_us, reserved = struct.unpack(SPI_HDR_FMT, payload[2:12])
    anchors = []
    off = SPI_ANC_OFF
    for _ in range(SPI_MAX_ANCHORS):
        has, tdoa, toa, az, el, rss = struct.unpack(ANCHOR_FMT, payload[off:off + ANCHOR_SIZE])
        anchors.append(Anchor(has, tdoa, toa, az, el, rss))
        off += ANCHOR_SIZE
    return SpiFrame(seq, mode_mask, n_anc, dt_us, anchors)


def parse_truth(payload: bytes) -> Truth:
    x, y, z, vx, vy, vz = struct.unpack(TRUTH_FMT, payload)
    return Truth(x, y, z, vx, vy, vz)


class FrameParser:
    def __init__(self):
        self.reset()

    def reset(self):
        self._buf = bytearray()

    def feed(self, data: bytes):
        self._buf.extend(data)
        out = []
        while True:
            i = 0
            while i + 1 < len(self._buf):
                if self._buf[i] == SYNC0 and self._buf[i + 1] == SYNC1:
                    break
                i += 1
            if i > 0:
                del self._buf[:i]
            if len(self._buf) < 2 + HDR_LEN:
                break
            plen = self._buf[3] | (self._buf[4] << 8)
            if plen > MAX_PAYLOAD:
                del self._buf[:1]
                continue
            total = 2 + HDR_LEN + plen + CRC_LEN
            if len(self._buf) < total:
                break
            frame = self._buf[:total]
            ftype = frame[2]
            seq = frame[5] | (frame[6] << 8)
            ts = struct.unpack_from("<I", frame, 7)[0]
            payload = bytes(frame[2 + HDR_LEN:2 + HDR_LEN + plen])
            crc_recv = frame[2 + HDR_LEN + plen] | (frame[2 + HDR_LEN + plen + 1] << 8)
            crc_calc = crc16(frame[2:2 + HDR_LEN + plen])
            if crc_recv == crc_calc:
                out.append((ftype, seq, ts, payload))
            else:
                out.append(("CRC_ERROR", crc_recv, crc_calc, None))
            del self._buf[:total]
        return out


# ===================== 仿真参考实现（与固件 sim.c 一致，供 Demo 模式） =====================
import math


def scene_init_state(scene):
    if scene == SCENE_CLIMB:
        return (6.0, 8.0, 4.0, 1.05, 0.65, 0.35)
    if scene == SCENE_TURN:
        return (10.0, 7.0, 5.5, 1.10, 0.25, 0.10)
    return (8.0, 10.0, 6.0, 1.20, 0.75, 0.20)  # STRAIGHT / default


class DemoSim:
    """本地仿真器：复刻固件 sim.c 的目标轨迹 + 每锚点测量，产出 SpiFrame 字节与 Truth。"""
    def __init__(self, cfg: Config):
        self.cfg = cfg
        self.rng = 0xA5C0FFEE
        self._rw = [0.0] * 6
        self.apply(cfg)

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

    def apply(self, cfg: Config):
        self.cfg = cfg
        if cfg.scene == SCENE_CUSTOM:
            st = (cfg.init_x, cfg.init_y, cfg.init_z, cfg.vel_x, cfg.vel_y, cfg.vel_z)
        else:
            st = scene_init_state(cfg.scene)
        self._rw = list(st)
        self._last_us = 0.0
        self._seq = 0

    def reset(self):
        self.apply(self.cfg)

    def step(self, now_us):
        cfg = self.cfg
        dt_us = now_us - self._last_us
        self._last_us = now_us
        if dt_us > 1e6:
            dt_us = 1e6
        dt = dt_us * 1e-6
        t = self._rw
        t[0] += t[3] * dt; t[1] += t[4] * dt; t[2] += t[5] * dt
        # bounce
        if t[0] > 40:  t[0] = 40;  t[3] = -t[3]
        if t[0] < -40: t[0] = -40; t[3] = -t[3]
        if t[1] > 40:  t[1] = 40;  t[4] = -t[4]
        if t[1] < -40: t[1] = -40; t[4] = -t[4]
        if t[2] > 20:  t[2] = 20;  t[5] = -t[5]
        if t[2] < 0.5: t[2] = 0.5; t[5] = -t[5]

        en = cfg.enable_mask
        # ref anchor range
        rdx, rdy, rdz = t[0] - ANCHORS[REF_ANC][0], t[1] - ANCHORS[REF_ANC][1], t[2] - ANCHORS[REF_ANC][2]
        rrho = math.sqrt(rdx*rdx + rdy*rdy) or 1e-3
        rrange = math.sqrt(rrho*rrho + rdz*rdz) or 1e-3

        buf = bytearray(SPI_FRAME_LEN)
        buf[0] = SPI_MAGIC0; buf[1] = SPI_MAGIC1
        struct.pack_into("<H", buf, 2, self._seq & 0xFFFF)
        buf[4] = en & 0xFF
        buf[5] = N_ANC
        struct.pack_into("<I", buf, 6, int(dt_us) & 0xFFFFFFFF)
        # reserved [10..11] = 0
        off = SPI_ANC_OFF   # 12
        for i in range(SPI_MAX_ANCHORS):
            if i < N_ANC:
                dx = t[0] - ANCHORS[i][0]; dy = t[1] - ANCHORS[i][1]; dz = t[2] - ANCHORS[i][2]
                rho = math.sqrt(dx*dx + dy*dy) or 1e-3
                rng = math.sqrt(rho*rho + dz*dz) or 1e-3
                has = 0; tdoa = 0.0; toa = 0.0; az = 0.0; el = 0.0; rss = 0.0
                if (en & SPI_MODE_TDOA) and i != REF_ANC:
                    has |= SPI_MODE_TDOA; tdoa = (rng - rrange) / C_LIGHT + self._gauss() * TDOA_STD
                if en & SPI_MODE_TOA:
                    has |= SPI_MODE_TOA; toa = rng / C_LIGHT + self._gauss() * TOA_STD
                if en & SPI_MODE_AOA:
                    has |= SPI_MODE_AOA; az = math.atan2(dy, dx) + self._gauss() * AOA_STD
                    el = math.atan2(dz, rho) + self._gauss() * AOA_STD
                if en & SPI_MODE_RSS:
                    has |= SPI_MODE_RSS; rss = RSS_REF - 10.0 * RSS_N * math.log10(rng) + self._gauss() * RSS_STD
                struct.pack_into(ANCHOR_FMT, buf, off, has, tdoa, toa, az, el, rss)
            off += ANCHOR_SIZE
        crc = crc16(bytes(buf[:180]))
        struct.pack_into("<H", buf, 180, crc)
        self._seq = (self._seq + 1) & 0xFFFF
        return bytes(buf), Truth(t[0], t[1], t[2], t[3], t[4], t[5])
