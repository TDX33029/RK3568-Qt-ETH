# Linux framebuffer 上板部署说明

本文档对应当前已经验证通过的 PS 端上板流程，适用于 Windows 交叉编译 + SD 卡部署 + Linux framebuffer 显示的使用方式。

## 1. Windows 端交叉编译

在 Windows PowerShell 中执行：

```powershell
$env:CROSS_PREFIX = "D:\Program Files (x86)\Arm\GNU Toolchain mingw-w64-i686-arm-none-linux-gnueabihf\bin\arm-none-linux-gnueabihf-"
cd C:\Users\xaq\Desktop\Graduation_Project\chapter5\ps_tracker
.\build_cross_arm.ps1
.\stage_for_usb.ps1
```

执行完成后，会生成：

- `build_arm\tracker_demo`
- `build_arm\fb_tracker_ui`
- `stage_usb\`

其中 `stage_usb` 为推荐拷贝目录。

## 2. 推荐复制位置

结合当前调试经验，推荐将以下文件复制到 SD 启动分区：

- `tracker_demo`
- `fb_tracker_ui`

推荐目标路径：

```bash
/media/sd-mmcblk0p1
```

这样做的原因：

- 根文件系统可能被重新挂载为只读，导致无法覆盖旧程序
- USB 盘在板端可能出现 FAT 读写异常
- SD 启动分区更稳定，也便于下次直接替换二进制

## 3. 板端基础检查

上板后先执行：

```bash
uname -a
ls -l /dev/fb0
```

如果 `/dev/fb0` 存在，说明 framebuffer 已经就绪，可以继续运行界面程序。

也可执行：

```bash
./board_probe.sh
```

重点检查：

- 系统已正常进入 Linux
- `/dev/fb0` 存在
- framebuffer 分辨率正常
- 输入设备存在

## 4. 运行命令行实验

推荐直接在 SD 启动分区运行新版程序：

```bash
cd /media/sd-mmcblk0p1
./tracker_demo --single --scene straight --tdoa --steps 80 --seed 1
./tracker_demo --single --scene straight --aoa --steps 80 --seed 1
```

如果需要多目标演示：

```bash
./tracker_demo --multi3 --scene straight --aoa --steps 80 --seed 1
```

## 5. 运行 LCD 交互界面

```bash
cd /media/sd-mmcblk0p1
./fb_tracker_ui
```

当前按键说明：

- `W` / `S`：菜单上下移动
- `A` / `D`：修改当前选项
- `Enter`：运行实验
- `M`：结果页返回菜单
- `R`：结果页重新运行
- `Q`：退出程序

当前菜单项：

- `SCENE`
- `TARGETS`
- `MODALITY`
- `STEPS`
- `SEED`
- `RUN`
- `QUIT`

## 6. 当前已经验证通过的现象

板端已经确认如下现象：

- `fb_tracker_ui` 运行后 LCD 能显示菜单界面
- 串口按 `W/S/A/D/Enter` 后界面有正常响应
- 结果页可以显示 `MODALITY`
- 结果页可以显示 `TOTAL xx.xxx ms`
- 结果页可以显示轨迹图

## 7. 常见问题处理

### 1. 运行后还是旧界面

优先检查是不是运行了旧路径下的旧二进制，例如：

- `/root/ps_tracker/fb_tracker_ui`
- `/media/sd-mmcblk0p1/fb_tracker_ui`

可使用下面命令比对：

```bash
ls -l /root/ps_tracker/fb_tracker_ui /media/sd-mmcblk0p1/fb_tracker_ui
md5sum /root/ps_tracker/fb_tracker_ui /media/sd-mmcblk0p1/fb_tracker_ui
```

建议始终直接运行 SD 分区里的最新版程序。

### 2. 根文件系统无法写入

如果出现：

- `Read-only file system`

则不要继续尝试覆盖 `/root/ps_tracker` 中的程序，改为：

- 直接从 `/media/sd-mmcblk0p1` 运行
- 或重新制作稳定的启动卡后再整理根文件系统

### 3. USB 盘无法读取

若出现：

- `FAT read failed`
- `I/O error, dev sda`
- `No medium found`

说明当前 USB 盘或供电状态不稳定。此时建议：

- 更换 U 盘
- 重新插拔后再查看 `dmesg`
- 不把运行目录长期放在 USB 上

## 8. 推荐的最终使用方式

对于当前阶段，推荐固定采用以下流程：

1. Windows 下交叉编译并生成 `stage_usb`
2. 将 `tracker_demo` 和 `fb_tracker_ui` 拷贝到 SD 启动分区
3. 板端从 `/media/sd-mmcblk0p1` 直接运行
4. 串口做输入，LCD 做显示

该流程已经实际验证通过，可作为第五章实验的标准部署流程。
