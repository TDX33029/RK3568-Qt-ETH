cmake_minimum_required(VERSION 3.16)
include_guard()

# =============================================================================
# Usage: cmake -B build_rk3568 -S . -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-linux-gnu.cmake
#
# Prerequisites (on Ubuntu 22.04 / Debian 12):
#   sudo apt install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu
#
# For a full sysroot with Qt5/Qt6 for RK3568, you can use:
#   - Yocto SDK (recommended): build an SDK with `bitbake meta-toolchain-qt6`
#   - Custom sysroot: populate /opt/rk3568_sysroot with the target's /
#
# If using Yocto SDK, source the environment script and omit the manual
# compiler paths below (the SDK sets CC/CXX for you).
# =============================================================================

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_C_COMPILER   aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
set(CMAKE_AR           aarch64-linux-gnu-ar)
set(CMAKE_RANLIB       aarch64-linux-gnu-ranlib)
set(CMAKE_STRIP        aarch64-linux-gnu-strip)

# ── Uncomment and adjust for your sysroot ──────────────────────────────────
# set(CMAKE_SYSROOT /opt/rk3568_sysroot)
# set(CMAKE_FIND_ROOT_PATH ${CMAKE_SYSROOT})
# set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
# set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
# set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
# set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# ── RK3568 CPU tuning ──────────────────────────────────────────────────────
set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS}   -march=armv8-a+crc -mtune=cortex-a55")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=armv8-a+crc -mtune=cortex-a55")
