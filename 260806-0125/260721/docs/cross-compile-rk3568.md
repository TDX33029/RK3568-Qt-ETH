# RK3568 (正点原子 ATK-DLRK3568) 交叉编译与 glibc 版本问题解决

## 1. 问题现象

交叉编译出的 `ps_tracker_ui` 拷到 RK3568 板子上运行,报:

```
./ps_tracker_ui: /lib/aarch64-linux-gnu/libc.so.6: version `GLIBC_2.34' not found (required by ./ps_tracker_ui)
# 或
./ps_tracker_ui: /lib/aarch64-linux-gnu/libstdc++.so.6: version `GLIBCXX_3.4.29' not found
# 或笼统报 "glibc version too low"
```

## 2. 根因(已实测确认)

**不是代码问题,是 sysroot(系统库)版本不匹配。**

- 本机 Ubuntu 22.04, 自带/默认的 aarch64 交叉工具链 sysroot 是 **glibc 2.35/2.36**。
- 正点原子官方工具链 `/opt/atk-dlrk3568-5_10_sdk-toolchain` 的 sysroot 也是 **glibc 2.36**(Buildroot, GCC 10.4, Qt 5.15.8)。
- glibc 2.34 起 crt 启动文件强制要求 `GLIBC_2.34`,所以**任何**用 glibc≥2.34 sysroot 链接的二进制,最低都要求 `GLIBC_2.34`。
- 板子若是 **Debian 10 = glibc 2.28**(或 Debian 11 = 2.31),则 `2.34 > 2.28/2.31` → 报错。

实测(本项目,官方工具链):

```
产物: ELF 64-bit aarch64, 依赖 Qt5Widgets/Gui/Core 5.15.8
所需最高 GLIBC: GLIBC_2.34   ← Debian 10(2.28)给不了
```

## 3. 先在板子上确认 glibc 版本(决定走哪条路)

在**板子**上执行:

```bash
ldd --version | head -1
# 例: "ldd (Debian GLIBC 2.28-10+deb10u1) 2.28"  → Debian 10, glibc 2.28
# 例: "ldd (GNU libc) 2.36"                       → Buildroot/Debian12, glibc 2.36
```

- 板子 glibc **≥ 2.34** → 走 **方案 A**(直接用官方工具链,最简单)。
- 板子 glibc **< 2.34**(Debian 10/11)→ 走 **方案一/方案 B**(用板子自己的 sysroot)。

## 4. 方案 A:板子 glibc ≥ 2.34(直接用官方工具链)

```bash
./scripts/build_rk3568.sh
# 产物: build_rk3568/ps_tracker_ui
```

Qt5 5.15.8 来自官方工具链 sysroot,无需额外配置。

## 5. 方案一(方案 B):板子 glibc < 2.34(Debian 10/11)—— 真正修好

核心思想:**链接板子自己的 glibc**,产物就只要求板子实际拥有的符号。

### 5.1 从板子拉取 sysroot

```bash
# 板子地址按实际改
./scripts/bootstrap_sysroot_rk3568.sh --host root@192.168.1.100

# 若板子上没装 Qt5 开发包,加 --install-qt 让脚本在板子上 apt 安装:
./scripts/bootstrap_sysroot_rk3568.sh --host root@192.168.1.100 --install-qt
```

脚本会:
1. SSH 到板子,打印板子真实 glibc 版本;
2. rsync 板子的 `/lib/aarch64-linux-gnu`、`/usr/lib/aarch64-linux-gnu`、`/usr/include`、Qt5 到 `/opt/rk3568_board_sysroot`;
3. 校验关键库,并报告板子 glibc 实际最高符号。

### 5.2 用板子 sysroot 构建

```bash
./scripts/build_rk3568.sh --sysroot /opt/rk3568_board_sysroot
```

构建后产物所需最高 GLIBC 应 ≤ 板子 glibc(例如 Debian 10 → `GLIBC_2.28`),可在板子上运行。

## 6. 部署到板子

```bash
scp build_rk3568/ps_tracker_ui root@<板子IP>:/root/
ssh root@<板子IP> '/root/ps_tracker_ui'   # 需 DISPLAY 环境,或用 -platform offscreen 测试
```

Qt5 运行库:
- 方案 A(官方工具链,Qt 来自 sysroot 2.36):板子需有同版本 Qt5 5.15.8 运行库。Buildroot 镜像自带;Debian 需 `apt install libqt5widgets5`。
- 方案一(板子 sysroot):板子就是库的来源,运行库天然匹配,通常无需额外拷贝。

## 7. 已对项目做的修改

| 文件 | 改动 |
|------|------|
| `CMakeLists.txt` | Qt 前缀改为可覆盖的 `QT_PREFIX` 缓存变量(原硬编码 x86-64 路径,无法用于 aarch64);修正 macOS 拼写 `GUI_IDENTIFIOR→IDENTIFIER` |
| `cmake/aarch64-linux-gnu.cmake` | 改用正点原子官方编译器(绝对路径,无需 `source env`);支持 `RK3568_SYSROOT` 覆盖(方案一);设 find-root-path 模式使 Qt5 在 sysroot 内被找到 |
| `scripts/build_rk3568.sh` | 交叉编译构建脚本(支持 `--sysroot`/`--clean`) |
| `scripts/bootstrap_sysroot_rk3568.sh` | 从板子 rsync 构造 sysroot 的引导脚本(方案一) |

源码与原配置已备份到 `backup_20260806_011319/`。

## 8. 常见错误速查

| 报错 | 原因 | 处理 |
|------|------|------|
| `GLIBC_2.34 not found` | 用了 glibc 2.36 的官方/Ubuntu sysroot,板子是 Debian 10/11 | 走方案一,加 `--sysroot` |
| `GLIBCXX_3.4.29 not found` | libstdc++ 太新 | 同上,板子 sysroot 一并解决 |
| `cannot find -lQt5Widgets` / `Qt5 not found` | sysroot 里没 Qt5 | 板子 `apt install qtbase5-dev` 后重跑 bootstrap 脚本 |
| cmake 提示 `aarch64-linux-gnu-g++` 找不到 | 用了旧工具链文件(已修复) | 现已指向正点原子官方编译器 |
| `cmake --build` 报 `Unknown argument -j` | `source environment-setup` 把 cmake 设成 alias | 脚本已用 `/usr/bin/cmake` 绕过;手动时勿 source env,或用 `\cmake` |
