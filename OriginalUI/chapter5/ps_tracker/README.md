# PS 端三维定位跟踪实验说明

本目录为第五章 PS 端实验工程，对应 Zynq/AC820 开发板 Linux 环境下的三维定位跟踪验证程序。当前工程已经完成以下内容：

- PS 端 C 语言自仿真，不依赖 MATLAB 预生成输入数据
- 单目标三维定位跟踪
- 固定三目标演示模式 `multi3`
- 单次实验只启用一种测量量，支持 `TDOA`、`TOA`、`AOA`、`RSS`
- 命令行实验程序 `tracker_demo`
- Linux framebuffer 交互界面 `fb_tracker_ui`
- 结果页显示 `XY`、`XZ` 和 `3D VIEW` 三种轨迹视图
- 输出位置误差、速度误差、总耗时和单步耗时

其中，论文正文实验部分建议重点使用“单目标 + TDOA / AOA”两种模式；`TOA`、`RSS` 和 `multi3` 可作为工程扩展能力与现场演示内容保留。

## 目录结构

- `tracker3d.h` / `tracker3d.c`
  三维状态模型、测量模型、EKF 预测更新和测量仿真。
- `tracker_app.h` / `tracker_app.c`
  上层实验封装，同时被命令行程序和界面程序复用。
- `main.c`
  命令行实验入口，支持场景、目标模式、测量模式、步数和随机种子配置。
- `fb_linux_main.c`
  基于 Linux framebuffer 的板载交互界面。
- `ui_draw.h` / `ui_draw.c`
  轻量绘图模块，负责文字、线段、矩形和简单图形绘制。
- `build_cross_arm.ps1`
  Windows 下交叉编译 ARM Linux 静态程序。
- `stage_for_usb.ps1`
  将上板所需文件集中到 `stage_usb` 目录。
- `DEPLOY_LINUX_FB.md`
  板端部署和运行说明。

## 当前功能说明

### 1. 运动场景

- `straight`：近似直线运动
- `turn`：存在平面转向机动
- `climb`：包含明显高度变化

### 2. 目标模式

- `single`：单目标实验模式
- `multi3`：固定三目标演示模式

### 3. 测量模式

每次实验只使用一种测量量：

- `TDOA`
- `TOA`
- `AOA`
- `RSS`

### 4. 结果输出

命令行模式输出：

- 场景名称
- 目标模式和目标数
- 测量类型和测量维数
- 分阶段估计结果
- 最终位置、速度、RMSE
- 总运行时间和单步平均时间

界面模式输出：

- 参数菜单页
- 结果页统计信息
- `XY VIEW`
- `XZ VIEW`
- `3D VIEW`

## Windows 交叉编译

推荐使用 Arm GNU Toolchain 的 `arm-none-linux-gnueabihf` 工具链，并采用静态链接，避免板端 `glibc` 版本不匹配。

```powershell
$env:CROSS_PREFIX = "D:\Program Files (x86)\Arm\GNU Toolchain mingw-w64-i686-arm-none-linux-gnueabihf\bin\arm-none-linux-gnueabihf-"
cd C:\Users\xaq\Desktop\Graduation_Project\chapter5\ps_tracker
.\build_cross_arm.ps1
.\stage_for_usb.ps1
```

生成结果：

- `build_arm\tracker_demo`
- `build_arm\fb_tracker_ui`
- `stage_usb\`

## 板端部署建议

结合当前实际调试过程，推荐优先将新版程序复制到 SD 启动分区 `/media/sd-mmcblk0p1` 后直接运行，原因如下：

- 根文件系统曾出现只读挂载，直接覆盖 `/root/ps_tracker` 中旧程序不稳定
- USB 盘在板端出现过 FAT 读写错误，不适合作为长期运行目录
- SD 启动分区稳定、易于直接替换新版本二进制

推荐板端运行目录：

```bash
cd /media/sd-mmcblk0p1
```

## 命令行运行示例

### 1. 单目标论文实验

```bash
./tracker_demo --single --scene straight --tdoa --steps 80 --seed 1
./tracker_demo --single --scene straight --aoa --steps 80 --seed 1
./tracker_demo --single --scene turn --tdoa --steps 60 --seed 1
./tracker_demo --single --scene turn --aoa --steps 60 --seed 1
./tracker_demo --single --scene climb --tdoa --steps 60 --seed 1
./tracker_demo --single --scene climb --aoa --steps 60 --seed 1
```

### 2. 多目标演示

```bash
./tracker_demo --multi3 --scene straight --aoa --steps 80 --seed 1
```

### 3. 导出轨迹数据

```bash
./tracker_demo --single --scene climb --aoa --steps 60 --seed 1 --csv result.csv
```

## framebuffer 界面运行

```bash
./fb_tracker_ui
```

当前按键定义：

- `W` / `S`：菜单上下移动
- `A` / `D`：修改当前选项
- `Enter`：运行实验
- `M`：结果页返回菜单
- `R`：结果页按当前参数重新运行
- `Q`：退出程序

菜单页当前包含：

- `SCENE`
- `TARGETS`
- `MODALITY`
- `STEPS`
- `SEED`
- `RUN`
- `QUIT`

## 已完成的板端验证

目前已经完成以下验证：

- Linux 系统可从 SD 卡正常启动
- 串口终端可正常登录
- `/dev/fb0` 可正常访问
- `fb_tracker_ui` 可在 LCD 上显示菜单和结果页
- 串口按键 `W/S/A/D/Enter` 可正常驱动界面
- `tracker_demo` 可在板端输出真实实验结果
- 界面结果页可显示 `MODALITY`、`TOTAL`、`STEP` 和轨迹图

## 论文实验建议

若用于论文正文，建议直接采用下面的实验组织方式：

- 目标模式：`single`
- 测量模式：`TDOA`、`AOA`
- 场景：`straight`、`turn`、`climb`
- 指标：位置 RMSE、速度 RMSE、总耗时、单步耗时

这样既能覆盖典型运动场景，又能突出角度测量相对时差测量的性能优势，同时和当前板端实测结果完全一致。
