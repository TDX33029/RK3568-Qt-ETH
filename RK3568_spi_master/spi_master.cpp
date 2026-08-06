/**
  ******************************************************************************
  * @file    spi_master.cpp
  * @brief   RK3568 SPI 主机：全双工轮询读取 F407 从机发来的 182B SpiFrame。
  *          - 时钟由 RK 产生，每个 CS 下降沿 F4 通过 TX-DMA 发出一帧。
  *          - 上电先同步握手：连读 1B，直到读到 SPI_SYNC_READY(0x00) 再进入帧读取。
  *          - 每帧做 magic + CRC16 校验，统计丢帧/错误并通过回调抛出。
  *
  *  用法:
  *    ./spi_master [--dev /dev/spidev2.0] [--hz 1000000] [--no-sync] [--decim N]
  *
  *  编译(RK3568, aarch64 Linux):
  *    aarch64-linux-gnu-g++ -O2 -std=c++17 spi_master.cpp -o spi_master
  *  或直接 g++（若在本机 ARM 环境）:
  *    g++ -O2 -std=c++17 spi_master.cpp -o spi_master
  ******************************************************************************
  */
#include "spi_protocol.h"

#include <fcntl.h>
#include <getopt.h>
#include <linux/spi/spidev.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

static volatile std::atomic<bool> g_running(true);

static void on_signal(int) { g_running = false; }

/* ----------------------- SPI 句柄 ----------------------- */
struct SpiDev {
    int fd = -1;
    uint32_t speed = 1000000;
    uint8_t  mode = SPI_MODE_0;   /* CPOL=0, CPHA=0 —— 与 F4 从机一致 */
    uint8_t  bits = 8;
};

static bool spi_open(SpiDev &dev, const char *path)
{
    dev.fd = open(path, O_RDWR);
    if (dev.fd < 0) {
        perror("open spidev");
        return false;
    }
    if (ioctl(dev.fd, SPI_IOC_WR_MODE, &dev.mode) < 0) { perror("WR_MODE"); return false; }
    if (ioctl(dev.fd, SPI_IOC_WR_BITS_PER_WORD, &dev.bits) < 0) { perror("WR_BITS"); return false; }
    if (ioctl(dev.fd, SPI_IOC_WR_MAX_SPEED_HZ, &dev.speed) < 0) { perror("WR_SPEED"); return false; }
    return true;
}

static void spi_close(SpiDev &dev) { if (dev.fd >= 0) close(dev.fd); dev.fd = -1; }

/* 全双工收发 1 个完整 transfer（tx 全 0 仅用于驱动 SCK）*/
static bool spi_xfer_full(const SpiDev &dev, uint8_t *buf, uint32_t len)
{
    struct spi_ioc_transfer tr {};
    memset(&tr, 0, sizeof(tr));
    tr.tx_buf   = (uintptr_t)0;              /* 主机不发送有效数据 */
    tr.rx_buf   = (uintptr_t)buf;
    tr.len      = len;
    tr.speed_hz = dev.speed;
    tr.bits_per_word = dev.bits;
    tr.delay_usecs  = 0;
    tr.cs_change = 1;                          /* 每次 transfer 结束拉高 CS —— F4 据此换帧 */
    if (ioctl(dev.fd, SPI_IOC_MESSAGE(1), &tr) < 1) {
        perror("SPI_IOC_MESSAGE");
        return false;
    }
    return true;
}

/* 同步握手：连读 1 字节，期望 0x00；若读到 0xA5 表示 F4 未就绪，继续等 */
static bool spi_wait_sync(const SpiDev &dev, int timeout_ms)
{
    uint8_t b = 0xFF;
    auto t0 = std::chrono::steady_clock::now();
    while (g_running) {
        if (!spi_xfer_full(dev, &b, 1)) return false;
        if (b == SPI_SYNC_READY)  return true;
        if (b != SPI_SYNC_BUSY) {
            /* 不是约定的状态字节：可能 F4 直接发帧了，交给后续帧对齐处理 */
            std::cerr << "[warn] 同步字节异常 0x" << std::hex << (int)b
                      << "，跳过握手，进入帧滑动对齐模式\n";
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        auto now = std::chrono::steady_clock::now();
        if (timeout_ms > 0 &&
            std::chrono::duration_cast<std::chrono::milliseconds>(now - t0).count() > timeout_ms) {
            std::cerr << "[err] 同步超时（F4 一直返回 BUSY）\n";
            return false;
        }
    }
    return false;
}

/* ----------------------- 帧读取 + 滑动对齐 ----------------------- */
/* 在 182B 流内找 magic 并对齐；返回 0 表示成功取出一帧到 out */
static int find_and_read_frame(const SpiDev &dev, SpiFrame &out)
{
    uint8_t tmp[SPI_FRAME_LEN];
    /* 先读一整块 */
    if (!spi_xfer_full(dev, tmp, sizeof(tmp))) return -1;
    /* 检查是否恰好对齐 */
    if (tmp[0] == SPI_MAGIC0 && tmp[1] == SPI_MAGIC1) {
        memcpy(&out, tmp, sizeof(out));
        return 0;
    }
    /* 滑动：逐字节读，直到头部出现 magic */
    for (int shift = 1; shift < SPI_FRAME_LEN; shift++) {
        memmove(tmp, tmp + 1, sizeof(tmp) - 1);
        uint8_t nb = 0;
        if (!spi_xfer_full(dev, &nb, 1)) return -1;
        tmp[sizeof(tmp) - 1] = nb;
        if (tmp[0] == SPI_MAGIC0 && tmp[1] == SPI_MAGIC1) {
            memcpy(&out, tmp, sizeof(out));
            return 0;
        }
    }
    return -2; /* 一个完整窗口内找不到 magic */
}

/* ----------------------- 回调（可被 Qt 工程复用）----------------------- */
typedef void (*FrameCb)(const SpiFrame *f, uint32_t idx, void *user);
typedef void (*DropCb)(uint32_t reason, void *user);   /* reason: 1=CRC, 2=无magic, 3=IO */

/* ----------------------- 主循环 ----------------------- */
int run(SpiDev &dev, bool do_sync, int decim, FrameCb fcb, DropCb dcb, void *user)
{
    if (do_sync) {
        std::cout << "[info] 同步握手中（等待 F4 就绪 0x00）...\n";
        if (!spi_wait_sync(dev, 5000)) return -1;
        std::cout << "[info] 握手完成，开始接收帧\n";
    }

    SpiFrame frame;
    uint32_t idx = 0, dec = 0;
    while (g_running) {
        int r = find_and_read_frame(dev, frame);
        if (r < 0) {
            if (dcb) dcb(3, user);          /* IO 错误 */
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        if (spi_frame_validate(&frame) != 0) {
            if (dcb) dcb(1, user);          /* CRC / magic 错误 */
            continue;
        }
        if (decim > 1 && (dec++ % decim) != 0) {
            idx++;
            continue;                        /* 抽稀：仅计数不回调 */
        }
        if (fcb) fcb(&frame, idx, user);
        idx++;
    }
    return 0;
}

/* ----------------------- 默认文本回调（命令行演示）----------------------- */
static void default_frame_cb(const SpiFrame *f, uint32_t idx, void * /*user*/)
{
    printf("[#%u] seq=%u n_anc=%u dt_us=%u mode=0x%02X crc=OK\n",
           idx, f->seq, f->n_anc, f->dt_us, f->mode_mask);
    for (int i = 0; i < f->n_anc && i < SPI_MAX_ANCHORS; i++) {
        const SpiAnchor &a = f->anchors[i];
        printf("   A%u has=0x%02X tdoa=%+.2e toa=%+.2e az=%.2f el=%.2f rss=%.1f\n",
               i, a.has, a.tdoa_sec, a.toa_sec, a.aoa_az, a.aoa_el, a.rss_dbm);
    }
    fflush(stdout);
}

static void default_drop_cb(uint32_t reason, void * /*user*/)
{
    static uint32_t cnt[4] = {0};
    cnt[reason]++;
    if ((cnt[reason] & 0x3F) == 1)
        fprintf(stderr, "[drop] reason=%u total=%u\n", reason, cnt[reason]);
}

/* ----------------------- 命令行入口 ----------------------- */
static void usage(const char *p)
{
    fprintf(stderr,
        "Usage: %s [options]\n"
        "  --dev   <path>  SPI 设备 (默认 /dev/spidev2.0)\n"
        "  --hz    <hz>    时钟频率 (默认 1000000)\n"
        "  --no-sync       跳过上电同步握手（直接滑窗对齐）\n"
        "  --decim <N>     每 N 帧仅回调 1 次 (默认 1)\n",
        p);
}

int main(int argc, char **argv)
{
    std::string devpath = "/dev/spidev2.0";
    uint32_t hz = 1000000;
    bool do_sync = true;
    int decim = 1;

    static struct option opts[] = {
        {"dev",   required_argument, 0, 'd'},
        {"hz",    required_argument, 0, 'z'},
        {"no-sync", no_argument,     0, 'n'},
        {"decim", required_argument, 0, 'm'},
        {"help",  no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };
    int c;
    while ((c = getopt_long(argc, argv, "d:z:nm:h", opts, nullptr)) != -1) {
        switch (c) {
            case 'd': devpath = optarg; break;
            case 'z': hz = (uint32_t)strtoul(optarg, nullptr, 10); break;
            case 'n': do_sync = false; break;
            case 'm': decim = atoi(optarg); if (decim < 1) decim = 1; break;
            default:  usage(argv[0]); return 1;
        }
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    SpiDev dev;
    dev.speed = hz;
    if (!spi_open(dev, devpath.c_str())) return -1;

    std::cout << "[info] SPI 主机启动: " << devpath
              << " @ " << hz << " Hz, mode=" << (int)dev.mode
              << ", 帧长=" << SPI_FRAME_LEN << "B\n";

    int r = run(dev, do_sync, decim, default_frame_cb, default_drop_cb, nullptr);

    spi_close(dev);
    std::cout << "\n[info] 退出\n";
    return r;
}
