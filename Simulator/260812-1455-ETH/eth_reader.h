#ifndef ETH_READER_H
#define ETH_READER_H

#include <QThread>
#include <QString>
#include <QByteArray>
#include <QMetaType>
#include <atomic>
#include <cstdint>

#include "alg/tracker3d.h"
#include "frame_protocol.h"

class EthReader : public QThread {
    Q_OBJECT
public:
    explicit EthReader(QObject *parent = nullptr);
    ~EthReader() override;

    void setHost(const QString &host) { m_host = host; }
    void setPort(quint16 port)        { m_port = port; }

    void stop() { m_stop.store(true); }

    quint32 pingCount() const { return m_pingCount.load(); }

signals:
    void frameReceived(const Tracker3DMeasurement &meas, double dt_sec, quint16 seq);
    void anchorsUpdated(const QByteArray &payload);
    void error(const QString &msg);

protected:
    void run() override;

private:
    QString          m_host      = QStringLiteral("0.0.0.0");
    quint16          m_port      = 5000;
    std::atomic<bool> m_stop     {false};
    std::atomic<quint32> m_pingCount {0};
    quint16          m_last_seq  = 0xFFFFu;
    bool             m_seen_first = false;
};

Q_DECLARE_METATYPE(Tracker3DMeasurement)

#endif
