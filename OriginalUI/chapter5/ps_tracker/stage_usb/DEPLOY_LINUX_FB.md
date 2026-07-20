# Linux Framebuffer 上板步骤

## 1. 把工程放到板子上

推荐把整个目录拷到板端，例如：

```bash
mkdir -p ~/ps_tracker
cd ~/ps_tracker
```

把以下文件至少带到板上：

- `tracker3d.h`
- `tracker3d.c`
- `tracker_app.h`
- `tracker_app.c`
- `ui_draw.h`
- `ui_draw.c`
- `fb_linux_main.c`
- `build_linux_fb.sh`
- `board_probe.sh`
- `run_linux_fb.sh`

## 2. 先检查板端 Linux 环境

执行：

```bash
chmod +x board_probe.sh
./board_probe.sh
```

重点看：

- 是否存在 `/dev/fb0`
- 是否存在 `/sys/class/graphics/fb0`
- 是否有 `gcc`
- 屏幕模式和位深是否正常

如果 `/dev/fb0` 不存在，说明 LCD 或 framebuffer 还没起来，这时先不要跑 UI。

## 3. 编译 framebuffer UI

执行：

```bash
chmod +x build_linux_fb.sh
./build_linux_fb.sh
```

成功后应看到类似输出：

```bash
Built: .../build_linux/fb_tracker_ui
```

## 4. 运行 UI

执行：

```bash
chmod +x run_linux_fb.sh
./run_linux_fb.sh
```

## 5. 当前按键说明

- `W` / `S`：上下移动菜单
- `A` / `D`：切换场景、勾选量测或调整数值
- `Enter`：运行当前实验
- `M`：从结果页回到菜单
- `R`：在结果页重新运行
- `Q`：退出

## 6. 如果 UI 没显示出来，优先检查

1. `/dev/fb0` 是否存在
2. 是否在本地串口终端直接运行，而不是纯 SSH
3. LCD 是否已经被 Linux 驱动正确点亮
4. framebuffer 位深是否为 `16` 或 `32`

## 7. 建议你回传给我的内容

如果你上板后要我继续排查，直接把这三部分发我：

1. `./board_probe.sh` 的完整输出
2. `./build_linux_fb.sh` 的输出
3. `./run_linux_fb.sh` 运行后的现象
   - 屏幕是否点亮
   - 菜单是否显示
   - 按键是否有效
