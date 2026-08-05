cmake_minimum_required(VERSION 3.16)
include_guard()

# =============================================================================
# Toolchain: 正点原子 ATK-DLRK3568 (aarch64, Buildroot GCC 10.4, kernel 5.10)
#
# 官方工具链: /opt/atk-dlrk3568-5_10_sdk-toolchain
#   - 自带 Qt 5.15.8 (含 CMake 配置) 在 sysroot 的 /usr 下
#   - sysroot glibc = 2.36 → 默认产物要求 GLIBC_2.34
#
# ⚠️ glibc 兼容性(报错 "glibc version too low" 的根因):
#   官方工具链 sysroot 的 glibc 是 2.36,链接后二进制最低要求 GLIBC_2.34。
#   若你的板子是 Debian 10 (glibc 2.28) 或 Debian 11 (glibc 2.31),
#   2.34 > 板子版本 → 仍会报 "version `GLIBC_2.34' not found"。
#   解决:用板子自己的根文件系统做 sysroot (方案一),见下方 RK3568_SYSROOT。
#
# 用法 A —— 板子是 ALIENTEK Buildroot 镜像或 Debian 12 (glibc ≥ 2.34):
#   cmake -B build_rk3568 -S . -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-linux-gnu.cmake
#   cmake --build build_rk3568 -j$(nproc)
#
# 用法 B —— 板子是 Debian 10/11 (glibc < 2.34),方案一:用板子 sysroot:
#   1) 先运行 scripts/bootstrap_sysroot_rk3568.sh  (rsync 板子 -> /opt/rk3568_board_sysroot)
#   2) cmake -B build_rk3568 -S . \
#        -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-linux-gnu.cmake \
#        -DRK3568_SYSROOT=/opt/rk3568_board_sysroot
#      cmake --build build_rk3568 -j$(nproc)
#
# 验证板子 glibc 版本(在板子上执行):
#   ldd --version        # 看第一行,例如 "ldd (Debian GLIBC 2.28-10+deb10u1) 2.28"
# =============================================================================

set(CMAKE_SYSTEM_NAME      Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# ── 正点原子官方交叉工具链(绝对路径,无需 source environment-setup) ──────────
set(ALIENTEK_TC "/opt/atk-dlrk3568-5_10_sdk-toolchain" CACHE PATH "ALIENTEK RK3568 toolchain root")
set(CMAKE_C_COMPILER   "${ALIENTEK_TC}/bin/aarch64-buildroot-linux-gnu-gcc")
set(CMAKE_CXX_COMPILER "${ALIENTEK_TC}/bin/aarch64-buildroot-linux-gnu-g++")
set(CMAKE_AR           "${ALIENTEK_TC}/bin/aarch64-buildroot-linux-gnu-ar")
set(CMAKE_RANLIB       "${ALIENTEK_TC}/bin/aarch64-buildroot-linux-gnu-ranlib")
set(CMAKE_STRIP        "${ALIENTEK_TC}/bin/aarch64-buildroot-linux-gnu-strip")
set(CMAKE_READELF      "${ALIENTEK_TC}/bin/aarch64-buildroot-linux-gnu-readelf")

# 官方工具链自带 sysroot (glibc 2.36, Qt 5.15.8)。用法 A 直接用它。
set(ALIENTEK_SYSROOT "${ALIENTEK_TC}/aarch64-buildroot-linux-gnu/sysroot")

# ── 方案一:用板子自己的根文件系统做 sysroot (修复 Debian 10 glibc 太低) ──────
# 通过 -DRK3568_SYSROOT=... 或环境变量 RK3568_SYSROOT 指定板子根文件系统。
# 设置后链接器改用板子的 glibc/libstdc++/Qt,产物只要求板子实际拥有的符号。
if(NOT DEFINED RK3568_SYSROOT AND DEFINED ENV{RK3568_SYSROOT})
    set(RK3568_SYSROOT "$ENV{RK3568_SYSROOT}")
endif()

if(RK3568_SYSROOT)
    set(CMAKE_SYSROOT        "${RK3568_SYSROOT}")
    set(CMAKE_FIND_ROOT_PATH "${RK3568_SYSROOT}")
    set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
    set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
    set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
    set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
    message(STATUS "RK3568: 使用板子 sysroot = ${RK3568_SYSROOT} (方案一, 适配 Debian 10/低 glibc)")
else()
    set(CMAKE_FIND_ROOT_PATH "${ALIENTEK_SYSROOT}")
    set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
    set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
    set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
    set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
    message(STATUS "RK3568: 使用官方工具链 sysroot (glibc 2.36) —— 产物要求 GLIBC_2.34,仅适配 glibc>=2.34 的镜像")
    message(STATUS "RK3568: 若板子是 Debian 10/11 (glibc<2.34),请设 -DRK3568_SYSROOT=/opt/rk3568_board_sysroot (方案一)")
endif()

# ── RK3568 CPU tuning (Cortex-A55) ──────────────────────────────────────────
set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS}   -march=armv8-a+crc -mtune=cortex-a55")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=armv8-a+crc -mtune=cortex-a55")
