# STM32F407ZGT6 仿真传感器固件

基于野火 StdPeriph 空白工程，新增连续 TOA/AOA/TDOA 仿真发生器，经 SPI 输出 + UART 受控/回传。

## 外设映射

| 外设 | 引脚 | 用途 |
|------|------|------|
| USART1 | PA9(TX) / PA10(RX) | 与上位机 UART（板载 USB-UART 调试口），115200 8N1 |
| SPI2 (主机, TX) | PB13 SCK / PB14 MISO / PB15 MOSI | 数据链路：把仿真帧连续打到 SPI 总线 |
| SPI2 CS | PB12 | 软件 CS，帧间拉高，帧内拉低 |
| SPI2 FRAME_SYNC | PB11 | 每帧拉高，供下游帧对齐 |
| TIM2 | - | 32bit 定时器，APB1=84MHz，按采样率触发帧生成 |
| SysTick | - | 1ms 节拍 / 时间戳 |
| GPIO PG7 | 蜂鸣器 | 启动两短鸣 + CRC 错误反馈（可由 `APP_BEEP_ENABLE` 关闭） |

> 引脚均可改：编辑对应 `bsp/*.c` 顶部的宏即可。

## 时钟
`bsp_clock.c` 用 **HSI->PLL 168MHz**，与板上是否焊接 HSE 晶振无关，UART/SPI 波特率确定。
（APB1=42MHz -> 定时器时钟 84MHz；APB2=84MHz；SPI2 时钟 42MHz，预分频 /8 => 5.25MHz。）

## 编译
Keil MDK 打开 `Project/RVMDK（uv5）/BH-F407.uvprojx` -> Build。新增源文件已在工程 APP 组中，
包含路径已加 `..\..\User\{bsp,proto,sim,app}`。

## 数据流
1. TIM2 按配置采样率触发，置位帧请求计数。
2. 主循环 `APP_Step` 取出请求 -> `SIM_Generate` 生成一帧 DATA_FRAME（与协议格式一致）。
3. `BSP_SPI_SendFrame` 经 SPI2 主机送出（CS + FRAME_SYNC 同步）。
4. 按 `telemetry_decim` 抽稀，把同一帧字节经 UART 回传上位机。

## 命令（UART，见 proto.h）
`PING / SET_CONFIG(38B) / START / STOP / RESET_SEQ / SET_RATE(4B) / GET_STATUS`。
应答 `PONG / ACK / NACK / STATUS(28B) / DATA_FRAME`。

## 占位数据载荷（后续替换）
`proto.h: ProtoDataFrame_t`：
```
n_targets, flags, reserved, frame_index, timestamp_ms        (12B)
per target(14B): toa(u16) aoa_az(i16) aoa_el(i16) tdoa(i16) snr(u16) pos_x(i16) pos_y(i16)
```
其中 `pos_x/pos_y` 为目标**真实 2D 位置 (m)**，由运动模型按场景几何生成（圆周/直线/静止/随机游走）。
改格式时：改 `ProtoDataFrame_t`、`sim.c` 填充（含 `motion_pos`）、上位机 `protocol.py: parse_data_frame`，
帧定界（SYNC/TYPE/LEN/SEQ/TS/CRC）保持不变。

## 性能与限制
- SPI 发送为轮询，单帧 ~30-90B @ 5.25MHz 约 50-140µs，适合 ≤2kHz 采样率。
- 主循环跟不上时丢弃多余请求并计入 `spi_overrun`（见 STATUS 帧）。
- 如需更高吞吐，把 `bsp_spi.c` 改为 DMA TX 即可（接口不变）。
