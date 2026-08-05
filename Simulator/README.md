# F407 仿真传感器上位机 + 固件

让 PC 上位机控制 STM32F407ZGT6，经 **SPI 连续发出** 仿真 TOA/AOA/TDOA 数据，
同时经 **UART** 接收上位机的配置 / 启停指令，并把生成的数据回传给上位机实时绘图。

```
 ┌────────────┐  UART(115200) 控制 + 遥测   ┌──────────────────────┐  SPI 连续数据帧   ┌────────────────┐
 │  PC 上位机 │ ◄──────────────────────────► │   STM32F407ZGT6      │ ────────────────► │ 下游真实系统    │
 │ host_gui.py│                              │  仿真 + 发生器固件    │  (SCK/MOSI/CS/FS) │ (受测设备/逻辑  │
 └────────────┘                              └──────────────────────┘                   │  分析仪)        │
                                              └─ TIM2 按采样率节拍生成帧                  └────────────────┘
```

> PC 没有 SPI 端口，因此 "用 SPI 发生数据" 指 F407 作 SPI **主机**，把仿真帧连续打到 SPI 总线供下游消费；
> 上位机的控制与可视化走 UART。两条链路使用**同一字节帧格式**（见下）。

## 目录

```
Simulator/
├── Host/                 # PC 上位机 (Python / PyQt5 / pyqtgraph / pyserial)
│   ├── host_gui.py       # 图形界面（含 Demo 模式，无硬件可独立演示）
│   ├── protocol.py       # 协议帧定义（与固件逐字节一致）
│   └── requirements.txt
└── FW/                    # STM32F407ZGT6 固件（基于野火 StdPeriph 空白工程）
    └── User/
        ├── main.c
        ├── stm32f4xx_it.c
        ├── proto/  (proto.h/.c)        帧格式 + CRC16
        ├── bsp/    (clock/uart/spi/timer)
        ├── sim/    (sim.h/.c)          TOA/AOA/TDOA 发生器
        ├── app/    (app.h/.c)          命令分发 + 主循环
        └── beep/   (原工程，复用为状态蜂鸣)
```

## 协议帧格式（UART 与 SPI 完全一致，小端）

```
 SYNC0 SYNC1 | TYPE | LEN(2) | SEQ(2) | TS(4 ms) | PAYLOAD(LEN) | CRC16(2)
 0xA5 0x5A                                                              CCITT-FALSE
```
CRC 覆盖 `TYPE..PAYLOAD`。完整定义见 `Host/protocol.py` 与 `FW/User/proto/proto.h`。

| 方向 | 类型 | 说明 |
|------|------|------|
| PC→F407 | 0x01 PING / 0x03 START / 0x04 STOP / 0x05 RESET_SEQ / 0x07 GET_STATUS | 控制 |
| PC→F407 | 0x02 SET_CONFIG(38B) / 0x06 SET_RATE(4B) | 配置 |
| F407→PC | 0x81 PONG / 0x82 ACK / 0x83 NACK / 0x84 STATUS(28B) | 应答 |
| F407→PC | 0x85 DATA_FRAME | 仿真数据帧（同时也是 SPI 链路上的字节流） |

> **数据载荷为占位实现**，后续真实格式只需改 `FW/User/proto/proto.h` 的 `ProtoDataFrame_t` /
> `Host/protocol.py` 的 `parse_data_frame` 与固件 `sim.c` 的填充逻辑，帧定界层不变。

## 快速开始

### 上位机（无硬件也能跑）
```bash
cd Simulator/Host
pip install -r requirements.txt
python host_gui.py
```
打开后点 **"启动 Demo 模式"** 即可本地生成 TOA/AOA/TDOA 并实时绘图。
连接真实 F407：选串口 → 连接 → 下发配置 → Start。

### 固件
用 Keil MDK 打开 `FW/Project/RVMDK（uv5）/BH-F407.uvprojx`，编译下载。
连线与外设映射见 `FW/README.md`。

详见各子目录 README。
