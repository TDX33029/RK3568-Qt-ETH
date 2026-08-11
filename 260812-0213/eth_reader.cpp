#include "eth_reader.h"

#include <cerrno>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>          /* fcntl / F_GETFL / F_SETFL / O_NONBLOCK */
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static const int kBufSize = 2048;
static const int kRcvBufSize = 4 * 1024 * 1024;   /* 加大接收缓冲, 防突发丢包 */

EthReader::EthReader(QObject *parent)
    : QThread(parent)
{
}

EthReader::~EthReader()
{
    m_stop.store(true);
    /* run() 内非阻塞 recvfrom + 2ms 轮询, stop 后最多 ~2ms 即退出;
     * 不用 quit() (本线程没有事件循环), 直接 wait 到退出, 避免线程
     * 仍在运行时对象已被销毁 (use-after-free)。 */
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
    /* 100Hz+ 帧率 + 偶发突发: 内核默认接收缓冲 (~212KB) 在 UI 忙时可能
     * 静默丢包, 显式放大。失败不致命, 忽略即可。 */
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

    /* 非阻塞接收, 便于在 m_stop 时及时退出 */
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
                msleep(2);                 /* 让出 CPU, 同时周期性检查 m_stop */
                continue;
            }
            if (errno == EINTR)
                continue;
            emit error(QStringLiteral("recvfrom 错误: %1").arg(strerror(errno)));
            break;
        }

        /* 联通测试: 收到 8B PING 帧即回 PONG (上位机测 RTT) */
        if (udp_ping_match(buf, (size_t)n)) {
            uint8_t pong[UDP_PING_LEN];
            udp_ping_to_pong(buf, pong);
            ::sendto(sock, (char *)pong, sizeof(pong), 0,
                     (struct sockaddr *)&from, fromlen);
            continue;
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
