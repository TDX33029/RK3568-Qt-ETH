/*
 * 板端 (RK3568) ETH1 接收测试 —— 取代原来的 SPI 测试 (spitest.c)
 * ----------------------------------------------------------------------------
 *   编译 (板子上直接用 g++):
 *     g++ ethtest.cpp tracker_app.cpp tracker3d.cpp -o ethtest -lm
 *   运行:
 *     ./ethtest                      # 绑定 0.0.0.0:5000, 只显示原始帧
 *     ./ethtest --port 5000 --bind 0.0.0.0
 *     ./ethtest --track              # 同时跑 EKF 实时定位, 显示估计位置
 *     ./ethtest --verbose            # 逐帧打印每个锚点的有效模态与数值
 *
 *   期望: 用 PC 端 Host/send_eth.py 往板子 IP:5000 发帧, 这里能收到并打印。
 *   本项目不使用 SPI, 数据完全走 ETH1/UDP。
 *     - 收到有效帧: 打印 seq / dt / n_anc / 各锚点数据
 *     - CRC 错误: 计入 crc_err, 不崩溃
 *     - 长度不对: 计入 len_err
 *     - seq 不走 / 跳变: 计入 drops (丢帧)
 *
 *   帧格式见 frame_protocol.h (182 字节 UdpFrame, 与传感器侧 / PC 端完全对齐)。
 * ============================================================================
 */
#include "frame_protocol.h"
#include "tracker_app.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

static void print_usage(const char *prog) {
    fprintf(stderr,
        "用法: %s [--bind ADDR] [--port PORT] [--track] [--verbose] [--modality tdoa|toa|aoa|rss] [--dt SEC]\n"
        "  --bind ADDR   绑定地址 (默认 0.0.0.0, 监听所有网卡含 ETH1)\n"
        "  --port PORT   监听 UDP 端口 (默认 5000)\n"
        "  --track       接 EKF 实时定位, 额外打印估计位置/速度\n"
        "  --verbose     逐帧打印每个锚点的有效模态与数值\n"
        "  --modality M  定位模态 (默认 tdoa)\n"
        "  --dt SEC      EKF 预测步长 (默认 0.1)\n",
        prog);
}

static void print_frame(const UdpFrame *f, int dropped, int verbose) {
    if (dropped > 0)
        printf("  [丢帧 %d]\n", dropped);

    printf("帧 seq=%5u  dt=%uus  n_anc=%u  mode_mask=0x%02x\n",
           f->seq, f->dt_us, f->n_anc, f->mode_mask);

    if (!verbose) return;
    for (int i = 0; i < UDP_MAX_ANCHORS; ++i) {
        uint8_t has = f->anchors[i].has;
        if (!has) continue;
        printf("   anc[%d]:", i);
        if (has & UDP_MODE_TDOA) printf(" tdoa=%.3f", (double)f->anchors[i].tdoa_sec);
        if (has & UDP_MODE_TOA)  printf(" toa=%.3f",  (double)f->anchors[i].toa_sec);
        if (has & UDP_MODE_AOA)  printf(" aoa=(%.3f,%.3f)", (double)f->anchors[i].aoa_az, (double)f->anchors[i].aoa_el);
        if (has & UDP_MODE_RSS)  printf(" rss=%.1f",  (double)f->anchors[i].rss_dbm);
        printf("\n");
    }
}

int main(int argc, char **argv) {
    const char *bind_addr = "0.0.0.0";
    int port = 5000;
    int do_track = 0;
    int verbose = 0;
    DemoScene  scene = SCENE_STRAIGHT;
    TrackerModality modality = MODALITY_TDOA;
    double dt = 0.1;

    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--bind") && i + 1 < argc)      bind_addr = argv[++i];
        else if (!strcmp(argv[i], "--port") && i + 1 < argc)  port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--track"))                 do_track = 1;
        else if (!strcmp(argv[i], "--verbose"))               verbose = 1;
        else if (!strcmp(argv[i], "--dt") && i + 1 < argc)    dt = atof(argv[++i]);
        else if (!strcmp(argv[i], "--modality") && i + 1 < argc) {
            if (tracker_parse_modality(argv[++i], &modality) != 0) { print_usage(argv[0]); return 2; }
        }
        else { print_usage(argv[0]); return 2; }
    }

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { perror("socket"); return 1; }

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons((uint16_t)port);
    sa.sin_addr.s_addr = inet_addr(bind_addr);
    if (bind(sock, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        perror("bind"); close(sock); return 1;
    }
    printf("[ethtest] 绑定 %s:%d 成功, 等待 ETH1/UDP 数据 (Ctrl+C 退出)...\n", bind_addr, port);

    /* 实时 EKF 初始化 (仅 --track) */
    TrackerSimResult res;
    TrackerSimOptions opts;
    tracker_sim_default_options(&opts);
    opts.scene = scene; opts.modality = modality; opts.dt = dt;
    if (do_track && tracker_live_init(&res, &opts) != 0) {
        fprintf(stderr, "EKF 初始化失败\n"); do_track = 0;
    }

    uint8_t buf[2048];   /* 大于帧长, 便于检测"超长数据报"而非误判为 CRC 错 */
    struct sockaddr_in from;
    socklen_t fromlen;

    uint32_t seq_prev = 0xFFFFFFFFu;
    long long stat_ok = 0, stat_crc = 0, stat_len = 0, stat_drop = 0;
    double last_report = 0;

    while (1) {
        fromlen = sizeof(from);   /* POSIX 值-结果参数, 每次调用前必须重置 */
        ssize_t n = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *)&from, &fromlen);
        if (n < 0) { perror("recvfrom"); break; }
        if (n != (ssize_t)sizeof(UdpFrame)) {
            stat_len++;
            fprintf(stderr, "长度错误: 收到 %zd 字节 (期望 %zu), 丢弃\n", n, sizeof(UdpFrame));
            continue;
        }

        UdpFrame *f = (UdpFrame *)buf;
        int v = udp_frame_valid(f);
        if (v != 0) {
            stat_crc++;
            fprintf(stderr, "CRC 错误 (magic=%02x%02x), 丢弃\n", f->magic[0], f->magic[1]);
            continue;
        }

        /* 丢帧检测: 以模 2^16 增量 d = seq - prev_seq 判定
         *   d==0      同帧重发 (传感器未刷新)  → 不计
         *   d==1      连续                     → 不计
         *   2<=d<2^15 小跳变                   → 计 d-1 帧丢失
         *   d>=2^15   大回退 (乱序/迟到旧帧)   → 不计
         * 2^15 半区分界在真丢帧 (通常个位数) 与乱序 (接近 65535) 之间有明显间隙。 */
        uint16_t prev_seq = (seq_prev == 0xFFFFFFFFu) ? 0xFFFFu : (uint16_t)seq_prev;
        int dropped = 0;
        if (prev_seq != 0xFFFFu) {
            uint16_t d = (uint16_t)((int)f->seq - (int)prev_seq);
            if (d >= 2 && d < 32768) dropped = (int)(d - 1);
        }
        if (dropped > 0) stat_drop += dropped;
        seq_prev = f->seq;

        stat_ok++;
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
        printf("[%s:%d] ", ip, ntohs(from.sin_port));
        print_frame(f, dropped, verbose);

        if (do_track) {
            Tracker3DMeasurement meas;
            udp_frame_to_measurement(f, &meas);
            double dtf = (f->dt_us > 0) ? (double)f->dt_us / 1e6 : dt;
            tracker_live_step(&res, &meas, dtf);
            const double *est = res.final_estimate;
            printf("  >> EKF 估计: pos=(%.3f, %.3f, %.3f)  vel=(%.3f, %.3f, %.3f)\n",
                   est[0], est[1], est[2], est[3], est[4], est[5]);
        }

        /* 每 1 秒汇报一次统计, 不打断逐帧输出 */
        double now = (double)time(NULL);
        if (now - last_report >= 1.0) {
            last_report = now;
            printf("── 统计: ok=%lld  crc_err=%lld  len_err=%lld  drop=%lld  last_seq=%u ──\n",
                   stat_ok, stat_crc, stat_len, stat_drop, f->seq);
        }
    }

    if (do_track) tracker_free_result(&res);
    close(sock);
    return 0;
}
