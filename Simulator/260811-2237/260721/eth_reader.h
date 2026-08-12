#ifndef ETH_READER_H
#define ETH_READER_H

#include <QThread>
#include <QString>
#include <QByteArray>
#include <QMetaType>
#include <atomic>
#include <cstdint>

#include "tracker3d.h"
#include "frame_protocol.h"

/*
 *  后台线程: 通过 ETH1 (UDP 套接字) 接收测量帧, 解析后经信号传给 UI 线程。
 *  仅 "Start Live" 后才打开套接字, 不影响仿真。
 *
 *  重要 (线程模型):
 *   - 本对象在 UI 线程创建 (MainWindow 构造), 其 QObject 的 thread affinity
 *     是 UI 线程, 但 run() 在工作线程执行, 信号从工作线程发射。
 *   - 因此连接必须显式用 Qt::QueuedConnection, 槽才会排队到 UI 线程执行;
 *     若不加 (AutoConnection 会按 affinity 判定为 DirectConnection), 槽将
 *     在接收线程里直接执行, 与 UI 线程争用状态, 属于数据竞争 (见 mainwindow.cpp)。
 *   - frameReceived 携带 Tracker3DMeasurement, 已用 Q_DECLARE_METATYPE 声明,
 *     MainWindow 构造时需 qRegisterMetaType<Tracker3DMeasurement>() 才能排队。
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

    /* 上位机联通探测 (PING) 累计次数 — UI 线程周期读取 (atomic 安全) */
    quint32 pingCount() const { return m_pingCount.load(); }

signals:
    /* 收到一帧有效测量 (UI 线程接收) */
    void frameReceived(const Tracker3DMeasurement &meas, double dt_sec, quint16 seq);
    /* 上位机下发的基站配置 (CFG 帧): payload = [n_anc:u8] + n_anc*UdpCfgAnchor
     * (QByteArray 内建类型, QueuedConnection 直接可用) */
    void anchorsUpdated(const QByteArray &payload);
    void error(const QString &msg);

protected:
    void run() override;

private:
    QString          m_host      = QStringLiteral("0.0.0.0");
    quint16          m_port      = 5000;         /* UDP 监听端口 */
    std::atomic<bool> m_stop     {false};
    std::atomic<quint32> m_pingCount {0};        /* 收到 PING 探测次数 */
    quint16          m_last_seq  = 0xFFFFu;      /* 上一有效 seq; 0xFFFF 视为首帧 */
    bool             m_seen_first = false;
};

/* 排队连接 (QueuedConnection) 需要: 编译期声明 + 运行期 qRegisterMetaType */
Q_DECLARE_METATYPE(Tracker3DMeasurement)

#endif // ETH_READER_H
