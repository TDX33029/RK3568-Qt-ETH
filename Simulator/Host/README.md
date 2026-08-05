# 上位机 Host GUI

基于 Python + PyQt5 + pyqtgraph + pyserial 的图形界面，控制 F407 仿真器并实时显示
TOA/AOA/TDOA 数据。内置 **Demo 模式**，无硬件时本地按与固件一致的运动模型生成数据。

## 安装与运行
```bash
pip install -r requirements.txt
python host_gui.py
```

## 界面

- **连接**：端口下拉框（自动枚举）、波特（默认 115200）、刷新、连接 / 断开。
- **Demo 模式**：勾选后本地生成数据，不依赖 F407。改完配置点 "下发配置" 即时生效。
- **仿真配置**：采样率、目标数、运动模型（静止/直线/圆周/随机游走）、运动周期、
  TOA/AOA/TDOA 的中心与量程、噪声 σ、遥测抽稀。
- **控制**：Start / Stop / Ping / Reset Seq / Get Status。
- **绘图**：TOA、AOA(方位+俯仰)、TDOA 三幅实时曲线，每目标一种颜色；**第 4 幅「实时位置」2D 图**显示目标轨迹（拖尾）+ 当前点 + 基站位置（红色三角）。
- **基站 Base Stations**：左侧可编辑（每行 `x, y`，单位 m），点"应用"更新图上标记。默认 4 个基站构成方形。
- **最新数据表**：每目标最近一帧的 TOA/AOA_az/AOA_el/TDOA/SNR/PosX/PosY。
- **原始帧 / 日志**：ACK/NACK/PONG/STATUS 解码、错误信息。
- **状态**：链路、接收帧数、实时速率、CRC 错误数、远端 F407 状态。

## 工作流（接真实 F407）
1. 串口选 F407 的 USB-UART 口（探索者板为 USART1 PA9/PA10），连接。
2. 调整配置 -> "下发配置 Send Config"（收到 ACK）。
3. 点 "Start"，曲线开始滚动。
4. 改采样率：直接改采样率框 -> 下发配置；或仅改速率用工具栏组合（SET_CONFIG 会重置全部参数）。

## Demo 模式与固件的一致性
`DemoGenerator` 的运动模型、噪声生成与 `FW/User/sim/sim.c` 一一对应，便于在没有板子时
验证上位机逻辑、调参、给下游演示。

## 协议
帧格式与字段定义全部在 `protocol.py`，与固件 `proto.h` 逐字节一致。改数据格式时两处同步即可。
