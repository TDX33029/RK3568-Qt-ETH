cmake_minimum_required(VERSION 3.16)
include_guard()

# =============================================================================
# CMake toolchain for RK3568 (aarch64) cross-compilation
#
# Usage:
#   cmake -B build_rk3568 -S . \
#       -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-linux-gnu.cmake \
#       -DCMAKE_PREFIX_PATH=/opt/rk3568_sysroot/usr/lib/cmake \
#       -DCMAKE_FIND_ROOT_PATH=/opt/rk3568_sysroot
#
# If using Yocto SDK, source the environment script first.
# It sets CC/CXX and CMAKE_PREFIX_PATH automatically.
# =============================================================================

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_C_COMPILER   aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
set(CMAKE_AR           aarch64-linux-gnu-ar)
set(CMAKE_RANLIB       aarch64-linux-gnu-ranlib)
set(CMAKE_STRIP        aarch64-linux-gnu-strip)

# ── Sysroot ─────────────────────────────────────────────────────────────────
# set(CMAKE_SYSROOT /opt/rk3568_sysroot)
# set(CMAKE_FIND_ROOT_PATH ${CMAKE_SYSROOT})
# set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
# set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
# set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
# set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# ── CPU tuning ──────────────────────────────────────────────────────────────
set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS}   -march=armv8-a+crc -mtune=cortex-a55")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=armv8-a+crc -mtune=cortex-a55")
