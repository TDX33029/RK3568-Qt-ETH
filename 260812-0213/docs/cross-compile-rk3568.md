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

- 本机 Ubuntu 22.04, 自带/默认的 aarch64 交叉工具链 sysroot 是 glibc 2.35/2.36。
- 正点原子官方工具链 `/opt/atk-dlrk3568-5_10_sdk-toolchain` 的 sysroot 也是 **glibc 2.36**(Buildroot, GCC 10.4, Qt 5.15.8)。
- glibc 2.34 起 crt 启动文件强制要求 `GLIBC_2.34`,所以**任何**用 glibc≥2.34 sysroot 链接的二进制,最低都要求 `GLIBC_2.34`。
- 板子若是 **Debian 10 = glibc 2.28**(或 Debian 11 = 2.31),则 `2.34 > 2.28/2.31` -> 报错。

实测(本项目,官方工具链):产物为 aarch64 ELF,依赖 Qt5 5.15.8,**所需最高 GLIBC = 2.34**,Debian 10(2.28)给不了。

## 3. 先在板子上确认 glibc 版本(决定走哪条路)

在**板子**上执行(板子自带终端/串口/HDMI,不用 SSH):

```bash
ldd --version | head -1
# "ldd (Debian GLIBC 2.28-...) 2.28"  -> Debian 10, 走方案一
# "ldd (GNU libc) 2.36"              -> Buildroot/Debian12, 走方案 A
```

- 板子 glibc **≥ 2.34** -> 走 **方案 A**(直接用官方工具链,最简单)。
- 板子 glibc **< 2.34**(Debian 10/11)-> 走 **方案一**(debootstrap chroot)。

## 4. 方案 A:板子 glibc ≥ 2.34(直接用官方工具链)

```bash
./scripts/build_rk3568.sh
# 产物: build_rk3568/ps_tracker_ui (需 GLIBC_2.34)
```
Qt5 5.15.8 来自官方工具链 sysroot,无需额外配置。

## 5. 方案一:板子 glibc < 2.34(Debian 10/11)-- 真正修好

核心思想:用 **Debian 10 (buster, glibc 2.28) 原生环境**构建,产物只要求 glibc 2.28。

### 5.1 推荐:本机 debootstrap arm64 chroot 原生构建(免 SSH、免板子)

SSH 账号验证过不去也没关系。不用跨编译器(跨编译器自带的 libstdc++/crt 会和新 sysroot 打架),直接在本机 debootstrap 一个 buster arm64 chroot,在 chroot 里用 qemu 原生编译 -- 编译器/库/Qt5 全是 buster 原生(glibc 2.28 + Qt5 5.11.3),绝无版本错配。

前置:本机已有 `debootstrap` + `qemu-aarch64-static` + binfmt(均已确认)。

```bash
# 首次: 建 chroot + 装 Qt5 + 构建(约 10~20 分钟, qemu 下 apt 较慢)
sudo bash scripts/build_debian10_chroot.sh
# 之后改了代码只重新构建(约 2~3 分钟)
sudo bash scripts/build_debian10_chroot.sh --build-only
```

脚本流程:
1. 装 `debootstrap`/`qemu-user-static`/`debian-archive-keyring`;
2. debootstrap buster arm64 到 `/opt/rk3568_buster_chroot`(qemu 第二阶段自动跑);
3. chroot 内 `apt install build-essential cmake qtbase5-dev`(Qt5 5.11.3 + glibc 2.28,全 buster 原生);
4. 源码 rsync 进 chroot,`cmake && make` 原生构建(gcc-8);
5. 产物拷出到 `build_debian10/ps_tracker_ui`,打印所需 GLIBC(应为 2.28)。

环境变量可覆盖 chroot 路径:`sudo RK3568_CHROOT=/path bash scripts/build_debian10_chroot.sh`。

### 5.2 备选:从板子 rsync 构造 sysroot(需 SSH 能通)

若 SSH 能用:`scripts/bootstrap_sysroot_rk3568.sh --host root@<板子IP>` 拉板子文件系统当 sysroot,再 `./scripts/build_rk3568.sh --sysroot /opt/rk3568_board_sysroot`。SSH 验证过不去就走 5.1。

## 6. 部署到板子

```bash
scp build_debian10/ps_tracker_ui root@<板子IP>:/root/   # 或 build_rk3568/ (方案A)
ssh root@<板子IP> '/root/ps_tracker_ui'   # 需 DISPLAY,或 -platform offscreen 测试
```

Qt5 运行库:方案一(Debian 10 chroot)产物的 Qt 是 5.11.3,板子需 `apt install libqt5widgets5`(Debian 10 自带同版本,通常已装或一键装)。

## 7. 已对项目做的修改

| 文件 | 改动 |
|------|------|
| `CMakeLists.txt` | Qt 前缀改为可覆盖的 `QT_PREFIX` 缓存变量(原硬编码 x86-64 路径,无法用于 aarch64);修正 macOS 拼写 `GUI_IDENTIFIOR->IDENTIFIER` |
| `cmake/aarch64-linux-gnu.cmake` | 改用正点原子官方编译器(绝对路径,无需 `source env`);支持 `RK3568_SYSROOT` 覆盖(方案一备选);设 find-root-path 模式使 Qt5 在 sysroot 内被找到 |
| `scripts/build_rk3568.sh` | 交叉编译构建脚本(方案 A,已实测通过) |
| `scripts/build_debian10_chroot.sh` | debootstrap buster arm64 chroot 原生构建脚本(方案一首选,免 SSH) |
| `scripts/bootstrap_sysroot_rk3568.sh` | 从板子 rsync 构造 sysroot(方案一备选,需 SSH) |

源码与原配置已备份到 `backup_20260806_011319/`。

## 8. 常见错误速查

| 报错 | 原因 | 处理 |
|------|------|------|
| `GLIBC_2.34 not found` | 用了 glibc 2.36 的官方/Ubuntu sysroot,板子是 Debian 10/11 | 走方案一 chroot: `sudo bash scripts/build_debian10_chroot.sh` |
| `GLIBCXX_3.4.29 not found` | libstdc++ 太新 | 同上 |
| `cannot find -lQt5Widgets` / `Qt5 not found` | sysroot 里没 Qt5 | chroot 脚本已 apt 装 qtbase5-dev |
| SSH 账号验证过不去 | 板子 SSH 密码/密钥问题 | 别用 rsync 脚本,走 5.1 chroot(完全免 SSH) |
| chroot 里 apt 报 Release expired | buster 已 EOL | 脚本已设 `Acquire::Check-Valid-Until "false"` |
