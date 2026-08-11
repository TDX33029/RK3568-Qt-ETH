#ifndef ETH_READER_H
#define ETH_READER_H

#include <QThread>
#include <QString>
#include <atomic>
#include <cstdint>

#include "tracker3d.h"
#include "frame_protocol.h"

/*
 *  后台线程: 通过 ETH1 (UDP 套接字) 接收测量帧, 解析后用 Qt 信号
 *  (跨线程自动排队) 传给 UI 线程。仅 "Start Live" 后才打开套接字, 不影响仿真。
 *
 *  RK3568 作为 UDP 服务端, 绑定到 ETH1 接口上的端口, 接收传感器发来的
 *  每帧 182 字节 (见 frame_protocol.h, 用 udp_frame_valid 做魔数/CRC/seq 校验)。
 *  若校验不通过或 seq 与上次相同(传感器未刷新)则丢弃。
 *
 *  数据帧格式见 docs/frame-protocol.md, 经 UDP 传输: 每个数据报恰好为一帧;
 *  若数据报长度与帧长不符则丢弃。本项目不使用 SPI。
 */
class EthReader : public QThread {
    Q_OBJECT
public:
    explicit EthReader(QObject *parent = nullptr);
    ~EthReader() override;

    /* 绑定地址: host="0.0.0.0" 监听所有接口; 指定 ETH1 的 IP 仅收该接口流量。
     * port 默认 5000。 */
    void setHost(const QString &host) { m_host = host; }
    void setPort(quint16 port)        { m_port = port; }

    void stop() { m_stop.store(true); }

signals:
    /* 收到一帧有效测量 (UI 线程接收) */
    void frameReceived(const Tracker3DMeasurement &meas, double dt_sec, quint16 seq);
    void error(const QString &msg);

protected:
    void run() override;

private:
    QString          m_host      = QStringLiteral("0.0.0.0");
    quint16          m_port      = 5000;         /* UDP 监听端口 */
    std::atomic<bool> m_stop     {false};
    quint16          m_last_seq  = 0xFFFFu;      /* 上一有效 seq; 0xFFFF 视为首帧 */
    bool             m_seen_first = false;
};

#endif // ETH_READER_H
