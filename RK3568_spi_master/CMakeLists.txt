cmake_minimum_required(VERSION 3.10)
project(rk3568_spi_master CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(spi_master
    spi_master.cpp
)

# RK3568 交叉编译（可选）：
#   cmake -B build -DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc \
#                    -DCMAKE_CXX_COMPILER=aarch64-linux-gnu-g++
# 本机 ARM 直接编译：
#   cmake -B build && cmake --build build
target_compile_options(spi_master PRIVATE -O2 -Wall)
