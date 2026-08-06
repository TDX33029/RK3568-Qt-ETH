# RK3568 SPI 主机程序（对接 F407 从机转发模式）

本目录包含 RK3568（SPI 主机）一侧的程序，与 `Simulator/FW/User/` 里已改造为
**转发模式**的 STM32F407 固件对接。

## 1. 角色与数据流

```
HOST(PC) --UART--> F407 --SPI(从机DMA)--> RK3568(主机轮询)
  下发 117B PUSH           转发 182B SpiFrame       逐帧读 + 校验
```

- F4 不再自产数据：HOST 通过 UART 下发实时测量帧（`CMD_PUSH_FRAME=0x08`，
  117B 压缩帧头+锚点），F4 用 `SpiFrame_FromPush` 还原成完整 182B 帧，
  存双缓冲 `SIM_PushLive`，由 SPI TX-DMA 在每次 CS 周期发出。
- RK3568 作为 SPI 主机，产生 SCK，每个 CS 下降沿触发 F4 发一帧 182B 数据。

## 2. 物理连线（F407 SPI2 从机 <-> RK3568 SPI 主机）

F4 端引脚来自 `bsp_spi.c`：SPI2 硬件 NSS 模式，GPIOB。

| 信号 | F407 (从机) | RK3568 (主机) | 说明 |
|------|-------------|---------------|------|
| SCK  | PB13        | SPIx_SCLK     | 由 RK3568 产生（主机时钟） |
| MOSI | PB15        | SPIx_MOSI     | 主机→从机（F4 忽略其数据，仅用于驱动时钟） |
| MISO | PC2         | SPIx_MISO     | 从机→主机（实际数据通道，原 PB14 不可用已改到 PC2） |
| NSS/CS | PB12     | SPIx_CS0      | 片选，RK3568 每个 transfer 拉低一次 |
| GND  | GND         | GND           | **必须共地**，否则电平参考错误 |

要点：
- **电平**：F407 为 3.3V IO，RK3568 GPIO 通常也是 3.3V（部分引脚 1.8V，需确认
  所用 SPI 引脚 bank 电压）。两者 3.3V 可直接相连；若 RK3568 侧为 1.8V 需加电平转换。
- **主机时钟方向**：SCK/MOSI/CS 由 RK3568 输出；MISO 由 F407 输出。不要把方向接反。
- **CS 极性**：F4 用硬件 NSS（低有效），RK3568 spidev 默认 CS 低有效，匹配。
- **速率**：程序默认 1 MHz；F4 从机无内部波特分频限制（由主机决定），1MHz 安全。
- **SPI 模式**：双方均为 **MODE0（CPOL=0, CPHA=0）**，已在 F4 `bsp_spi.c` 与
  本程序 `SPI_MODE_0` 中固定，勿改。

RK3568 设备树需启用对应 SPI 控制器并加载 `spidev` 驱动，例如
`/dev/spidev2.0`（具体总线号取决于板级 DTB）。

## 3. 同步握手

F4 上电时 SPI 从机 DMA 已装载首帧并常开 SPE，但首帧内容在 HOST 下发 PUSH 之前
是初始化 0 帧（magic 仍为 `A5 5A`）。本程序默认开启上电同步：

- 连读 1 字节，直到读到 `SPI_SYNC_READY=0x00` 再进入帧读取。
- 若读到 `SPI_SYNC_BUSY=0xA5`（F4 未就绪/未收到 HOST 配置），继续等待（最多 5s）。
- 可用 `--no-sync` 跳过握手，直接靠 magic+CRC 在字节流中滑动对齐。

> 注：当前 F4 固件并未真的在首字节发 0xA5/0x00 区分（DMA 直接发 182B 帧），
> 因此实际运行时握手字节通常是帧首 `A5 5A` 的 `A5`，程序会识别为“异常状态字节”
> 并自动转入**滑动对齐模式**，功能不受影响。若希望严格握手，可在 F4 的
> `BSP_SPI_Arm` 前将首字节置 0xA5、收到 START/PUSH 后再置 0x00 —— 见下方“可选增强”。

## 4. 帧格式（182B，小端，CRC16-CCITT-FALSE）

与 `Simulator/Host/protocol.py`、`260806-0232- 完备/spi_protocol.h` 逐字节一致：

```
magic   : u16  0xA55A
seq     : u16
mode_mask: u8
n_anc   : u8
dt_us   : u32
reserved: u16
anchors : 8 × 21B  { has:u8, tdoa:f32, toa:f32, aoa_az:f32, aoa_el:f32, rss:f32 }
crc     : u16  覆盖前 180 字节
```

验证：`spi_frame_validate()` 检查 magic 与 CRC。

## 5. 编译运行

```bash
# RK3568 本机
sudo apt-get install -y g++ cmake
cmake -B build && cmake --build build
sudo ./build/spi_master --dev /dev/spidev2.0 --hz 1000000

# 或交叉编译
aarch64-linux-gnu-g++ -O2 -std=c++17 spi_master.cpp -o spi_master
```

参数：`--dev` 设备路径、`--hz` 速率、`--no-sync` 跳过握手、`--decim N` 抽稀回调。

## 6. 与 Qt 工程（260806-0232- 完备）复用

`SpiFrame` / `SpiAnchor` 结构体与 `spi_protocol.h` 完全一致，Qt 工程可直接
`#include "spi_protocol.h"` 并把 `spi_master.cpp` 中的 `run()` + 回调
（`FrameCb`/`DropCb`）接入其 `DataParser`/显示管线，替代旧的 `SpiReader`。

## 7. 可选增强（F4 侧，如需严格握手）

在 `Simulator/FW/User/bsp/bsp_spi.c` 的 `BSP_SPI_Init` 末尾、装载首帧前，
将 `SIM_GetLiveFrame()` 首字节临时写为 `SPI_SYNC_BUSY`；在 `app.c` 收到
`PROTO_CMD_START` 或首次 `PROTO_CMD_PUSH_FRAME` 后，把 live 帧首字节写为
`SPI_SYNC_READY`。这样 RK3568 的握手逻辑即可严格区分就绪状态。
```
```
（此增强非必需，当前滑动对齐已能正常工作。）
