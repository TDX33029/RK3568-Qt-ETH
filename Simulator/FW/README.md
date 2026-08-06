# STM32F407ZGT6 仿真传感器固件 (RK3568 SPI 接收端配套)

F407 作 **SPI 从机**，向 RK3568 (主机) 连续返回固定 182 字节 `SpiFrame`，
内含 5 个锚点各自的 TOA/TDOA/AOA/RSS 仿真测量（由目标轨迹 + 锚点几何算出）。
PC 上位机经 UART 控制/监视。

## 外设映射

| 外设 | 引脚 | 用途 |
|------|------|------|
| USART1 | PA9(TX) / PA10(RX) | 与上位机 UART，115200 8N1 |
| **SPI2 (从机)** | PB13 SCK / **PC2 MISO** / PB15 MOSI / **PB12 NSS(硬件)** | RK3568 主机轮询读 182B |
| TIM2 | - | 32bit，按采样率驱动 SIM_Step 生成帧 |
| SysTick | - | 1ms 节拍 + µs 时间戳 |
| GPIO PG7 | 蜂鸣器 | 启动短鸣 + CRC 错误反馈 |

> SPI 接 RK3568 的 `/dev/spidev1.0` (SPI1, CS0)。模式 0, 8bit, MSB, ≤8MHz。
> F407 用 **MISO(PC2)** 发数据；NSS 硬件从机选通；MOSI 忽略（RK 发全 0）。

## SPI 帧 (182 字节, 小端, 与 RK spi_protocol.h 逐字节一致)

```
[0..1]   magic {0xA5,0x5A}
[2..3]   seq      u16
[4]      mode_mask u8  (bit0 TDOA bit1 TOA bit2 AOA bit3 RSS)
[5]      n_anc    u8   (5; 始终传 8 槽)
[6..9]   dt_us    u32  (距上帧微秒)
[10..11] reserved u16  (0)
[12..179] anchors[8] 每个 21B:
           has u8 | tdoa_sec f32 | toa_sec f32 | aoa_az f32 | aoa_el f32 | rss_dbm f32
[180..181] crc16 u16  CRC-16/CCITT-FALSE 覆盖 [0..179]
```

## 锚点 (与 RK configure_demo_anchors 相同, 索引/坐标必须一致)

| idx | x | y | z | 备注 |
|---|---|---|---|---|
| 0 | 0 | 0 | 0 | 参考锚 (TDOA 基准, 无 TDOA) |
| 1 | 30 | 0 | 4 | |
| 2 | 30 | 30 | 0 | |
| 3 | 0 | 30 | 5 | |
| 4 | 15 | 15 | 12 | |

## 测量模型 (与 RK tracker3d_simulate_measurement 一致)

对每锚点 i（目标位置 x,y,z）：
- `dx=x-ax, dy=y-ay, dz=z-az; ρ=√(dx²+dy²); range=√(ρ²+dz²)`
- `TOA = range/c`（c=299792458，噪声 σ=2ns）
- `TDOA = (range−range_0)/c`（i≠0，σ=2ns）
- `AOA_az=atan2(dy,dx)`，`AOA_el=atan2(dz,ρ)`（σ=1°）
- `RSS = −35 − 20·log10(range)`（σ=2dB）

## 数据流 / 双缓冲

- TIM2 按采样率触发 -> 主循环 `SIM_Step` 更新目标轨迹、填 `SpiFrame`、算 CRC、写 **shadow** 缓冲后**原子交换** `g_live`。
- SPI2 从机 **TX-DMA (DMA1_Stream4/Ch0)** 从 `g_live` 送出 182B；DMA TC 中断重装最新 `g_live`。RK 每次读到一帧完整数据。
- `seq` 每生成一帧 +1；RK 据此判新帧（未变则丢弃）。
- UART 每帧回传小 `TRUTH` (24B, 目标位置/速度, 上位机丝滑绘图)；`SpiFrame` 抽稀回传 (默认每 10 帧) 供监视。

## 时钟
HSI->PLL 168MHz（与板上晶振无关），UART/SPI 确定。

## 编译
Keil 打开 `Project/RVMDK（uv5）/BH-F407.uvprojx`。新增源文件已在 APP 组，包含路径已含子目录。

## UART 命令 (见 proto.h)
`PING / SET_CONFIG(32B) / START / STOP / RESET_SEQ / SET_RATE(4B) / GET_STATUS`
配置含：采样率、场景(STRAIGHT/CLIMB/TURN/CUSTOM)、模态位、抽稀、自定义初值/速度。
