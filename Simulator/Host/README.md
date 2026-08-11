# 上位机 Host

两个上位机:
- **`eth_host.py`** — **当前主用**, ETH1/UDP 链路上位机 (RK3568 定位接收端配套)。
  本项目不使用 SPI, PC 经 UDP 把 182 字节测量帧发给 RK3568 的 ETH1:5000,
  板端校验后喂 EKF 实时定位显示。
- `host_gui.py` — 旧 UART-F407 链路上位机 (控制 F407 仿真器, 仅供历史/F407 链路使用)。

## eth_host.py (ETH1/UDP, 主用)

### 安装与运行
```bash
pip install PyQt5 pyqtgraph        # 只需这两个 (不再需要 pyserial)
python eth_host.py
```

### 界面与用法
- **目标**: 板子 ETH1 IP + UDP 端口 (默认 `192.168.1.10:5000`)。
- **联通测试 🔍**: 一键验证链路 — ICMP ping (网络层可达) + UDP PING/PONG
  (应用层: 板端 5000 端口确认在收, 显示 RTT)。
- **仿真配置**: 场景 (直线/爬升/转弯/自定义)、采样率 (1~200Hz)、
  模态 (TDOA/TOA/AOA/RSS 勾选)、自定义初值/速度 (仅 Custom 场景使用)。
- **控制**: `▶ 启动发送` 开始按设定速率 UDP 发帧; `■ 停止`; `🧹 清空` 轨迹。
- **位置图**: 真值轨迹 + 当前点 + 锚点 A0~A4 (本地参照, 与板端 EKF 估计对照)。
- **状态 (发/收统计)**: 链路状态灯 / 已发帧数 / 实际发送速率 / 最新 seq /
  发送错误 (sendto 失败会弹窗并停止) / 板卡应答 PONG 次数 / 最近 RTT。
  发送期间每 2s 自动发一次心跳探测, 状态灯三态:
  `● 双向通 (板端在收)` 绿 = 3s 内有 PONG; `● 发送中, 无应答` 黄 = 板端没在收;
  `● 已停止` 灰。

### 联通测试原理 (PING/PONG, 8 字节)
板端 `frame_protocol.h` 定义了与 182B 数据帧区分的 8B 探测帧:
`{0xA5,0x5A,'P','I','N','G',seq,0}`; 板端 `eth_reader`/`ethtest` 收到即回
`{0xA5,0x5A,'P','O','N','G',seq,0}` (seq 回显), 上位机据此算 RTT。
> 板端代码已更新, **需要在板上重新编译**: `eth_reader.cpp`/`ethtest.cpp`/
> `frame_protocol.h` 三者同步 (240 工程内), 旧版板端程序不回 PONG。

### 帧格式
每个 UDP 数据报 = 一整帧 182 字节 `UdpFrame`
(`{magic, seq, mode_mask, n_anc, dt_us, anchors[8], crc16}`, 小端),
与板端 `260721/frame_protocol.h` 逐字节一致。`dt_us` 由单调时钟差分自动计算,
发送节奏 (QTimer 1ms 轮询 + 单调时钟节流) 与帧内 `dt_us` 解耦, 速率稳定准确。

### 联调
```bash
# 板端: 命令行测试 (逐帧打印 + PING 应答)
./ethtest --verbose --bind 0.0.0.0 --port 5000

# 板端: Qt 上位机 (自动进入实时接收)
./ps_tracker_ui --auto-live        # 或界面点 "Start Live (ETH1)"

# PC 端
python eth_host.py                 # 填板子 ETH1 IP -> 🔍 联通测试 -> ▶ 启动发送
```
联通测试显示 `✅ 双向通` 即板端 5000 端口确认在收; 板端打印
`[LIVE] seq=... est=(...)` / 逐帧数据 / `<- PING -> PONG` 即链路打通。

> **板端显示注意**: 若板子 HDMI 画面卡死/新终端无显示 (显示栈固有问题),
> 数据链路本身不受影响 — 用串口/SSH 观察板端 stdout 的 `[LIVE]` 输出即可;
> 程序用 `-platform linuxfb` 或 `-platform offscreen` 启动可绕开问题显示栈。

### 命令行发帧 (无 GUI)
```bash
python send_eth.py --target 192.168.1.10 --port 5000 --rate 100 --scene 0
```

## host_gui.py (旧 UART-F407 链路)

基于 Python + PyQt5 + pyqtgraph + pyserial 的图形界面，控制 F407 仿真器并实时显示
TOA/AOA/TDOA 数据。内置 **Demo 模式**，无硬件时本地按与固件一致的运动模型生成数据。

### 安装与运行
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
