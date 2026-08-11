# 测量帧协议 (ETH1/UDP) — 传感器 ↔ RK3568

RK3568 端代码在 `frame_protocol.h` / `eth_reader.cpp`(Qt 上位机实时源) 与 `ethtest.cpp`(板端 C 接收测试程序)。传感器端按本协议实现即可。

## 1. 角色 / 电气

| 项 | 值 |
|---|---|
| 角色 | **RK3568 = UDP 服务端(接收), 传感器 = UDP 客户端(发送)** |
| 介质 | ETH1 网络接口, UDP 数据报 |
| 端口 | RK3568 在 ETH1 上 `bind` UDP 端口 (默认 5000) |
| 帧传输 | **每个 UDP 数据报恰好一帧 182 字节** (传感器→RK, 单向) |
| 帧格式 | `{magic, seq, mode_mask, n_anc, dt_us, anchors[8], crc16}` |

> RK 端 `EthReader` (`eth_reader.cpp`) 在 ETH1 接口上 `recvfrom` 接收。**传感器侧**: 每当产生新测量, 用 UDP 把 182 字节帧发给 RK 的 ETH1 IP:Port 即可; 也可内部双缓冲 (`seq` 未变时 RK 端会丢弃)。RK 用 `seq` 判断帧是否更新(未变则丢弃)。

## 2. 帧布局(固定 182 字节, 小端序)

| 偏移 | 长度 | 字段 | 说明 |
|---:|---:|---|---|
| 0  | 2 | `magic` | `{0xA5, 0x5A}` 帧同步 |
| 2  | 2 | `seq` (u16) | 帧序号, 每产生一帧 +1 (回绕) |
| 4  | 1 | `mode_mask` (u8) | 传感器提供的模态位图: bit0 TDOA, bit1 TOA, bit2 AOA, bit3 RSS |
| 5  | 1 | `n_anc` (u8) | 实际有数据的锚点数 (信息用; **始终占 8 个锚点槽**) |
| 6  | 4 | `dt_us` (u32) | 距上一帧的微秒数, 用于 EKF 预测 |
| 10 | 2 | `reserved` | 0 |
| 12 | 168 | `anchors[8]` | 8 个锚点, 每个 21 字节 (见下) |
| 180 | 2 | `crc16` (u16) | CRC-16/CCITT-FALSE, 覆盖字节 [0..179] |
| **合计** | **182** | | |

**每个锚点 (21 字节)**:

| 偏移 | 长度 | 字段 | 说明 |
|---:|---:|---|---|
| 0 | 1 | `has` (u8) | bit0 tdoa, bit1 toa, bit2 aoa, bit3 rss (本锚点哪些测量有效) |
| 1 | 4 | `tdoa_sec` (f32) | 秒 (has&1 时有效) |
| 5 | 4 | `toa_sec`  (f32) | 秒 (has&2) |
| 9 | 4 | `aoa_az`   (f32) | 弧度 (has&4) |
| 13| 4 | `aoa_el`   (f32) | 弧度 (has&4) |
| 17| 4 | `rss_dbm`  (f32) | dBm (has&8) |

## 3. 锚点索引与几何

RK 端锚点坐标见 `tracker_app.cpp::configure_demo_anchors()`(传感器端必须用**相同索引和坐标**):

| idx | x | y | z | 备注 |
|---:|---:|---:|---:|---|
| 0 | 0 | 0 | 0 | **参考锚 (ref_anchor)** — TDOA 以它为基准 |
| 1 | 30 | 0 | 4 | |
| 2 | 30 | 30 | 0 | |
| 3 | 0 | 30 | 5 | |
| 4 | 15 | 15 | 12 | |

> 实际部署时把 `configure_demo_anchors` 里的坐标改成真实基站坐标即可(协议不变, 只是填对索引)。

## 4. 测量单位约定(重要)

- **TDOA**: 相对参考锚 A0 的时间差,**秒**。`tdoa[i] = (range_i − range_0)/c`。
- **TOA**: 到达时间,**秒**。`toa[i] = range_i / c`。(c=299792458 m/s)
- **AOA**: 方位角 `aoa_az = atan2(dy,dx)`, 俯仰角 `aoa_el = atan2(dz,ρ)`, **弧度**, 以锚点为原点指向目标。
- **RSS**: 接收功率, **dBm**。模型 `rss = ref_dbm − 10·n·log10(range)`。
- 当前选中模态在菜单页选 (TDOA/TOA/AOA/RSS), RK 端 EKF 只用选中模态的测量; 其余 `has` 位可置 0。

## 5. CRC-16/CCITT-FALSE

poly=0x1021, init=0xFFFF, refIn=false, refOut=false, xorOut=0x0000。覆盖字节 [0..179], 结果填入 [180..181] (小端)。

参考实现 (RK 与传感器端一致):
```c
uint16_t crc16_ccitt(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; ++b)
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    return crc;
}
```

## 6. 传感器端参考 C 结构(与 `frame_protocol.h` 一致)

```c
#pragma pack(push, 1)
typedef struct {
    uint8_t  has;
    float    tdoa_sec, toa_sec, aoa_az, aoa_el, rss_dbm;
} UdpAnchor;          /* 21 字节 */

typedef struct {
    uint8_t   magic[2];   /* {0xA5,0x5A} */
    uint16_t  seq;
    uint8_t   mode_mask;
    uint8_t   n_anc;
    uint32_t  dt_us;
    uint16_t  reserved;
    UdpAnchor anchors[8];
    uint16_t  crc16;
} UdpFrame;              /* 182 字节 */
#pragma pack(pop)
```

传感器端伪代码:
```c
static UdpFrame g_frame;       /* 当前帧 */
static uint16_t g_seq = 0;

/* 每当产生新测量时调用 */
void sensor_update_measurement(/* ...your measurement... */) {
    UdpFrame f = {0};
    f.magic[0]=0xA5; f.magic[1]=0x5A;
    f.seq = g_seq++;
    f.mode_mask = UDP_MODE_TDOA;            /* 例: TDOA */
    f.n_anc = 5;
    f.dt_us = 10000;                        /* 10ms */
    for (int i=0;i<5;i++){
        f.anchors[i].has = UDP_MODE_TDOA;
        f.anchors[i].tdoa_sec = tdoa_value[i];   /* 秒 */
    }
    f.crc16 = crc16_ccitt((uint8_t*)&f, offsetof(UdpFrame, crc16));
    /* 原子更新当前帧 (关中断或双缓冲交换) */
    memcpy(&g_frame, &f, sizeof(f));
}

/* UDP 发送: 每个数据报发一帧 g_frame (182 字节) 到 RK 的 ETH1 IP:端口 */
```

## 7. RK3568 侧运行要求

- ETH1 网络接口已配置 IP (例如 `192.168.1.10/24`), 与传感器在同一网段可达。
- RK3568 作为 UDP 服务端绑定 ETH1 端口 (默认 5000); 监听地址/端口可在 `MainWindow::startLive()` 里改 (`m_eth->setHost(...)` / `m_eth->setPort(...)`)。
- 传感器侧用 UDP 把每帧 182 字节发往 RK 的 ETH1 IP:Port。
- 启动程序后, 菜单页点 **"Start Live (ETH1)"** 即开始接收并实时显示。
- 若绑定失败 (端口被占/无权限), 弹窗报错并退出实时模式 (不影响仿真)。

## 8. 板端 C 接收测试程序 (ethtest.cpp)

除 Qt 上位机 `ps_tracker_ui` 外, 仓库另含一个**纯 C++ 的板端命令行程序** `ethtest.cpp`,
用于在 RK3568 上脱离 Qt/显示, 直接通过 ETH1 UDP 接收并显示数据, 验证链路与帧内容。

**编译 (板子上):**
```sh
g++ ethtest.cpp tracker_app.cpp tracker3d.cpp -o ethtest -lm
```

**运行:**
```sh
./ethtest                       # 绑定 0.0.0.0:5000, 显示收到的帧
./ethtest --port 5000 --bind 0.0.0.0
./ethtest --track               # 同时跑 EKF 实时定位, 显示估计位置/速度
./ethtest --verbose             # 逐帧打印每个锚点的有效模态与数值
./ethtest --modality tdoa --dt 0.1
```

**行为:**
- 每收到一帧: 校验魔数 + CRC16 (`udp_frame_valid`), 失败计入 `crc_err`/`len_err` 并丢弃。
- 用 `seq` 做丢帧检测 (未 +1 即记 `drop`)。
- `--verbose` 时打印每锚点的 `tdoa/toa/aoa/rss` 有效值。
- `--track` 时把帧转成 `Tracker3DMeasurement` (`udp_frame_to_measurement`) 喂给
  `tracker_live_init` / `tracker_live_step` 跑 EKF, 打印估计 `pos/vel`。
- 每秒打印统计: `ok / crc_err / len_err / drop / last_seq`。
- 帧格式、端口 (5000)、CRC 与 `frame_protocol.h`、传感器侧、`Host/send_eth.py`、Qt `eth_reader` 完全对齐。

**联调:** 板端跑 `./ethtest --verbose`, PC 端跑 `python3 Host/send_eth.py --target <板子IP>:5000`,
即可在板子终端看到逐帧数据; 这是验证"板子和电脑通了、数据正确"的最直接方式。

## 8.1 联通测试帧 (PING/PONG, 8 字节)

除 182B 数据帧外, 板端还响应 8B 探测帧 (上位机联通测试用):

| 偏移 | 字节 | 内容 |
|---:|---:|---|
| 0..1 | 2 | magic `{0xA5, 0x5A}` |
| 2..5 | 4 | `"PING"` (上位机→板子) / `"PONG"` (板子回) |
| 6 | 1 | seq (u8, 回显, 上位机算 RTT) |
| 7 | 1 | 0 |

板端 `eth_reader` / `ethtest` 收到 `"PING"` 即向源地址回 `"PONG"`,
不经过 182B 帧的 CRC/长度校验路径。上位机 `Host/eth_host.py` 发送中每 2s
自动心跳探测一次, "联通测试"按钮可手动触发 (同时做 ICMP ping)。
> 若板端程序未更新到含 PING/PONG 的版本, 上位机将收不到 PONG。

## 9. 丢帧/异常处理

- 魔数不符 → 丢弃该帧。
- CRC 不符 → 丢弃。
- `seq` 与上次相同 → 传感器未更新, 跳过 (RK 不显示陈旧数据)。
- EKF `update` 数值异常 → 不中断跟踪, 等待下一好帧。
- UDP 绑定失败 (端口被占/无权限) → 打印 `bind` 错误并退出。
