/*
 * 最小 SPI 测试 (隔离验证: 不依赖 Qt/显示)
 *   编译: gcc spitest.c -o spitest
 *   运行: sudo ./spitest [/dev/spidev1.0]
 *   期望: 打印 ioctl ret=0 + rx 字节, 不 Oops
 *   若 Oops -> SPI 驱动仍有问题; 若 ret=-1 -> open/权限问题; 若 ret=0 -> SPI 通路 OK
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>

int main(int argc, char **argv)
{
    const char *dev = argc > 1 ? argv[1] : "/dev/spidev1.0";
    int fd = open(dev, O_RDWR);
    if (fd < 0) { perror("open"); return 1; }
    printf("opened %s, fd=%d\n", dev, fd);

    /* 设置模式/字宽/速率 (可选, ioctl 传输里也带) */
    unsigned char mode = 0, bits = 8;
    unsigned int speed = 8000000;
    ioctl(fd, SPI_IOC_WR_MODE, &mode);
    ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits);
    ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed);

    /* 182 字节全双工 (和程序实际帧一样) */
    unsigned char tx[182], rx[182];
    memset(tx, 0, sizeof(tx));
    memset(rx, 0, sizeof(rx));

    struct spi_ioc_transfer t;
    memset(&t, 0, sizeof(t));
    t.tx_buf = (unsigned long)tx;
    t.rx_buf = (unsigned long)rx;
    t.len = 182;
    t.speed_hz = speed;
    t.bits_per_word = 8;

    printf("ioctl SPI_IOC_MESSAGE(1) ... ");
    fflush(stdout);
    int r = ioctl(fd, SPI_IOC_MESSAGE(1), &t);
    printf("ret=%d\n", r);
    if (r < 1) {
        perror("ioctl");
    } else {
        printf("rx[0..7] = %02x %02x %02x %02x %02x %02x %02x %02x\n",
               rx[0], rx[1], rx[2], rx[3], rx[4], rx[5], rx[6], rx[7]);
        /* 若传感器在发数据, rx 开头应是帧魔数 a5 5a */
        if (rx[0] == 0xa5 && rx[1] == 0x5a)
            printf("✓ 收到有效帧魔数 a5 5a, SPI 通路正常\n");
        else
            printf("(rx[0:1] 非 a5 5a, 传感器可能没发数据/没接好, 但 SPI 驱动本身没 Oops 就行)\n");
    }
    close(fd);
    return (r < 1) ? 1 : 0;
}
