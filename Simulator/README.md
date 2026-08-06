# F407 仿真传感器上位机 + 固件 (RK3568 SPI 接收端配套)

F407ZGT6 作 **SPI 从机**，向 RK3568 (主机) 连续返回固定 **182 字节 SpiFrame**，
内含 5 个锚点各自的 TOA/TDOA/AOA/RSS 仿真测量（由目标轨迹 + 锚点几何算出，模型与 RK 端一致）。
PC 上位机经 UART 控制 F407 并接收目标真实位置(Truth)用于绘图。

```
 ┌────────────┐  UART 控制+遥测   ┌──────────────────┐  SPI(从机->主机)  ┌────────────┐
 │  PC 上位机 │ ◄───────────────► │  STM32F407ZGT6   │ ─────────────────► │  RK3568    │
 │ host_gui.py│                   │  仿真发生器固件    │  PB13/PC2/PB15/PB12    │ (SPI 主机) │
 └────────────┘                   │  TIM2->SIM_Step   │  182B SpiFrame     └────────────┘
                                  └──────────────────┘
                                   双缓冲 + TX-DMA 响应轮询
```

> RK3568 每 10ms 全双工读 182B（模式0, 8bit, MSB, ≤8MHz）；F407 经 MISO 返回当前帧，
> RK 用 seq 判新帧。`SpiFrame` 定义 F407 与 RK 共用（`FW/User/proto/proto.h` ↔ RK `spi_protocol.h`）。

## 目录
```
Simulator/
├── Host/        # PC 上位机 (PyQt5/pyqtgraph/pyserial)
│   ├── host_gui.py     # 位置图(目标+锚点) + 每锚点测量曲线 + Demo 模式
│   └── protocol.py     # SpiFrame/Config/Truth 解析 + DemoSim(复刻固件)
└── FW/           # STM32F407 固件 (野火 StdPeriph 空白工程)
    └── User/{proto,bsp,sim,app,beep}
```

## 快速开始
- 上位机：`cd Host && pip install -r requirements.txt && python host_gui.py` -> 点"启动 Demo"（无硬件本地仿真）。
- 固件：Keil 打开 `FW/Project/RVMDK（uv5）/BH-F407.uvprojx` 编译下载。
- SPI 帧 / 锚点 / 测量模型 / UART 命令见 `FW/README.md`。
