#include "eth_reader.h"

#include <cerrno>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static const int kBufSize = 2048;
static const int kRcvBufSize = 4 * 1024 * 1024;

EthReader::EthReader(QObject *parent)
    : QThread(parent)
{
}

EthReader::~EthReader()
{
    m_stop.store(true);
    if (isRunning())
        wait();
}

void EthReader::run()
{
    m_stop.store(false);
    m_seen_first = false;

    int sock = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        emit error(QStringLiteral("无法创建 UDP 套接字: %1").arg(strerror(errno)));
        return;
    }

    int one = 1;
    ::setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    ::setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &kRcvBufSize, sizeof(kRcvBufSize));

    struct sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(m_port);
    addr.sin_addr.s_addr = inet_addr(m_host.toUtf8().constData());
    if (addr.sin_addr.s_addr == INADDR_NONE) {
        ::close(sock);
        emit error(QStringLiteral("非法监听地址: %1").arg(m_host));
        return;
    }

    if (::bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        emit error(QStringLiteral("无法在 %1:%2 绑定 UDP 套接字: %3")
                       .arg(m_host).arg(m_port).arg(strerror(errno)));
        ::close(sock);
        return;
    }

    int flags = ::fcntl(sock, F_GETFL, 0);
    ::fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    quint8 buf[kBufSize];
    while (!m_stop.load()) {
        struct sockaddr_in from{};
        socklen_t fromlen = sizeof(from);
        ssize_t n = ::recvfrom(sock, (char *)buf, sizeof(buf), 0,
                               (struct sockaddr *)&from, &fromlen);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                msleep(2);
                continue;
            }
            if (errno == EINTR)
                continue;
            emit error(QStringLiteral("recvfrom 错误: %1").arg(strerror(errno)));
            break;
        }

        if (udp_ping_match(buf, (size_t)n)) {
            uint8_t pong[UDP_PING_LEN];
            udp_ping_to_pong(buf, pong);
            ::sendto(sock, (char *)pong, sizeof(pong), 0,
                     (struct sockaddr *)&from, fromlen);
            m_pingCount.fetch_add(1);
            continue;
        }

        if (udp_cfg_match(buf, (size_t)n)) {
            UdpCfgAnchor anc[UDP_MAX_ANCHORS];
            int nanc = udp_cfg_parse(buf, anc, UDP_MAX_ANCHORS);
            uint8_t ack[UDP_PING_LEN];
            udp_cfg_to_ack(buf, ack);
            ::sendto(sock, (char *)ack, sizeof(ack), 0,
                     (struct sockaddr *)&from, fromlen);
            if (nanc > 0) {
                QByteArray payload;
                payload.append((char)nanc);
                payload.append((const char *)anc, nanc * (int)sizeof(UdpCfgAnchor));
                emit anchorsUpdated(payload);
            }
            continue;
        }

        if (n != (ssize_t)sizeof(UdpFrame))
            continue;

        UdpFrame f;
        std::memcpy(&f, buf, sizeof(f));

        if (udp_frame_valid(&f) != 0)
            continue;

        if (m_seen_first && f.seq == m_last_seq)
            continue;
        m_last_seq   = f.seq;
        m_seen_first = true;

        Tracker3DMeasurement meas;
        udp_frame_to_measurement(&f, &meas);

        double dt = f.dt_us / 1e6;
        if (dt <= 0.0) dt = 0.01;
        if (dt > 1.0)  dt = 1.0;

        emit frameReceived(meas, dt, f.seq);
    }

    ::close(sock);
}
