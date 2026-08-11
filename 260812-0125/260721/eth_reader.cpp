#include "eth_reader.h"

#include <QDateTime>

#include <cerrno>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static const int kBufSize = 2048;

EthReader::EthReader(QObject *parent)
    : QThread(parent)
{
}

EthReader::~EthReader()
{
    m_stop.store(true);
    if (isRunning()) {
        quit();
        wait(1000);
    }
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

    /* 非阻塞接收, 便于在 m_stop 时及时退出 */
    int flags = ::fcntl(sock, F_GETFL, 0);
    ::fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    quint8 buf[kBufSize];
    while (!m_stop.load()) {
        ssize_t n = ::recvfrom(sock, (char *)buf, sizeof(buf), 0, nullptr, nullptr);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                msleep(2);                 /* 让出 CPU, 同时周期性检查 m_stop */
                continue;
            }
            if (errno == EINTR)
                continue;
            emit error(QStringLiteral("recvfrom 错误: %1").arg(strerror(errno)));
            break;
        }

        if (n != (ssize_t)sizeof(UdpFrame))   /* 一数据报即一帧, 长度必须匹配 */
            continue;

        UdpFrame f;
        std::memcpy(&f, buf, sizeof(f));

        /* 校验魔数 / CRC / seq (见 frame_protocol.h) */
        if (!udp_frame_valid(&f))
            continue;

        if (m_seen_first && f.seq == m_last_seq)
            continue;                         /* seq 未更新 (传感器未刷新) */
        m_last_seq   = f.seq;
        m_seen_first = true;

        /* 转为 EKF 测量 */
        Tracker3DMeasurement meas;
        udp_frame_to_measurement(&f, &meas);

        double dt = f.dt_us / 1e6;
        if (dt <= 0.0) dt = 0.01;
        if (dt > 1.0)  dt = 1.0;

        emit frameReceived(meas, dt, f.seq);
    }

    ::close(sock);
}
