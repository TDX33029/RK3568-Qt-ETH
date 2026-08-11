#include "spi_reader.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#include <cstring>
#include <vector>

SpiReader::SpiReader(QObject *parent) : QThread(parent) {}

SpiReader::~SpiReader() {
    stop();
    wait();
}

void SpiReader::run() {
    int fd = open(m_device.toUtf8().constData(), O_RDWR);
    if (fd < 0) {
        emit error(QStringLiteral("打开 %1 失败: %2 (确认设备树启用 SPI1, 且当前用户对设备有读写权限)")
                   .arg(m_device).arg(QString::fromLocal8Bit(strerror(errno))));
        return;
    }

    uint8_t  mode = SPI_MODE_0;
    uint8_t  bits = 8;
    uint32_t speed = m_speed_hz;

    auto ioctl_check = [&](unsigned long req, void *arg, const char *what) -> bool {
        if (ioctl(fd, req, arg) < 0) {
            emit error(QStringLiteral("%1 失败: %2").arg(QString::fromLatin1(what))
                       .arg(QString::fromLocal8Bit(strerror(errno))));
            return false;
        }
        return true;
    };
    if (!ioctl_check(SPI_IOC_WR_MODE,        &mode,  "SPI_IOC_WR_MODE"))        { close(fd); return; }
    if (!ioctl_check(SPI_IOC_WR_BITS_PER_WORD, &bits, "SPI_IOC_WR_BITS_PER_WORD")) { close(fd); return; }
    if (!ioctl_check(SPI_IOC_WR_MAX_SPEED_HZ, &speed,"SPI_IOC_WR_MAX_SPEED_HZ")){ close(fd); return; }

    const int len = (int)sizeof(SpiFrame);
    std::vector<uint8_t> tx(len, 0);   /* 主机发送全 0 (传感器忽略 MOSI) */
    std::vector<uint8_t> rx(len, 0);

    while (!m_stop.load()) {
        struct spi_ioc_transfer tr;
        memset(&tr, 0, sizeof(tr));
        tr.tx_buf        = reinterpret_cast<uint64_t>(tx.data());
        tr.rx_buf        = reinterpret_cast<uint64_t>(rx.data());
        tr.len           = (uint32_t)len;
        tr.speed_hz      = m_speed_hz;
        tr.bits_per_word = 8;
        tr.delay_usecs   = 0;

        int ret = ioctl(fd, SPI_IOC_MESSAGE(1), &tr);
        if (ret < 1) {
            if (errno == EINTR) continue;
            emit error(QStringLiteral("SPI 传输失败: %1").arg(QString::fromLocal8Bit(strerror(errno))));
            break;
        }

        SpiFrame frame;
        memcpy(&frame, rx.data(), sizeof(frame));

        if (spi_frame_valid(&frame) != 0) {
            /* 魔数/CRC 不符, 丢弃 (主从帧对齐通常稳定; 偶发噪声自动跳过) */
            continue;
        }

        if (m_seen_first && frame.seq == m_last_seq) {
            /* seq 未变: 传感器还没产生新帧, 跳过 */
            QThread::usleep(1000);  /* 避免忙等 */
            continue;
        }
        m_last_seq   = frame.seq;
        m_seen_first = true;

        Tracker3DMeasurement meas;
        spi_frame_to_measurement(&frame, &meas);
        const double dt_sec = (double)frame.dt_us * 1e-6;
        emit frameReceived(meas, dt_sec, frame.seq);

        QThread::msleep(m_period_ms);
    }

    close(fd);
}
