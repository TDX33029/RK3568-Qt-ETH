#ifndef SPI_READER_H
#define SPI_READER_H

#include <QThread>
#include <QString>
#include <atomic>
#include <cstdint>

#include "tracker3d.h"
#include "spi_protocol.h"

/*
 *  后台线程: 通过 spidev 从 SPI1 轮询读取测量帧, 解析后用 Qt 信号
 *  (跨线程自动排队) 传给 UI 线程。仅 "Start Live" 后才打开设备, 不影响仿真。
 *
 *  RK3568 为 SPI 主机; 每次 SPI_IOC_MESSAGE 全双工收 182 字节。
 *  若魔数/CRC 不符丢弃; 若 seq 与上次相同(传感器未刷新)丢弃。
 */
class SpiReader : public QThread {
    Q_OBJECT
public:
    explicit SpiReader(QObject *parent = nullptr);
    ~SpiReader() override;

    void setDevice(const QString &dev) { m_device = dev; }
    void setSpeedHz(uint32_t hz)       { m_speed_hz = hz; }
    void setPollPeriodMs(int ms)       { m_period_ms = ms; }

    void stop() { m_stop.store(true); }

signals:
    /* 收到一帧有效测量 (UI 线程接收) */
    void frameReceived(const Tracker3DMeasurement &meas, double dt_sec, quint16 seq);
    void error(const QString &msg);

protected:
    void run() override;

private:
    QString          m_device    = QStringLiteral("/dev/spidev1.0");
    uint32_t         m_speed_hz  = 8000000;   /* 8 MHz */
    int              m_period_ms = 30;        /* ~33 Hz 轮询 (原 10ms/100Hz 对小核 + 显示栈压力过大) */
    std::atomic<bool> m_stop      {false};
    quint16          m_last_seq  = 0xFFFFu;   /* 上一有效 seq; 0xFFFF 视为首帧 */
    bool             m_seen_first = false;
};

#endif // SPI_READER_H
