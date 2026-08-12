#include "frame_protocol.h"
#include "alg/tracker_app.h"

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
        "  --bind ADDR   绑定地址 (默认 0.0.0.0)\n"
        "  --port PORT   监听 UDP 端口 (默认 5000)\n"
        "  --track       接 EKF 实时定位\n"
        "  --verbose     逐帧打印锚点数据\n"
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

    TrackerSimResult res;
    TrackerSimOptions opts;
    tracker_sim_default_options(&opts);
    opts.scene = scene; opts.modality = modality; opts.dt = dt;
    if (do_track && tracker_live_init(&res, &opts) != 0) {
        fprintf(stderr, "EKF 初始化失败\n"); do_track = 0;
    }

    uint8_t buf[2048];
    struct sockaddr_in from;
    socklen_t fromlen;

    uint32_t seq_prev = 0xFFFFFFFFu;
    long long stat_ok = 0, stat_crc = 0, stat_len = 0, stat_drop = 0, stat_ping = 0;
    double last_report = 0;

    while (1) {
        fromlen = sizeof(from);
        ssize_t n = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *)&from, &fromlen);
        if (n < 0) { perror("recvfrom"); break; }

        if (udp_ping_match(buf, (size_t)n)) {
            uint8_t pong[UDP_PING_LEN];
            udp_ping_to_pong(buf, pong);
            sendto(sock, (char *)pong, sizeof(pong), 0, (struct sockaddr *)&from, fromlen);
            stat_ping++;
            char pip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &from.sin_addr, pip, sizeof(pip));
            printf("[%s:%d] <- PING seq=%u -> PONG\n", pip, ntohs(from.sin_port), buf[6]);
            continue;
        }

        if (udp_cfg_match(buf, (size_t)n)) {
            UdpCfgAnchor anc[UDP_MAX_ANCHORS];
            int nanc = udp_cfg_parse(buf, anc, UDP_MAX_ANCHORS);
            uint8_t ack[UDP_PING_LEN];
            udp_cfg_to_ack(buf, ack);
            sendto(sock, (char *)ack, sizeof(ack), 0, (struct sockaddr *)&from, fromlen);
            if (nanc > 0) {
                printf("[%s] <- CFG n=%d A0=(%.1f,%.1f,%.1f) -> ACK\n",
                       "eth", nanc, anc[0].x, anc[0].y, anc[0].z);
            }
            continue;
        }

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

        double now = (double)time(NULL);
        if (now - last_report >= 1.0) {
            last_report = now;
            printf("── 统计: ok=%lld  crc_err=%lld  len_err=%lld  drop=%lld  ping=%lld  last_seq=%u ──\n",
                   stat_ok, stat_crc, stat_len, stat_drop, stat_ping, f->seq);
        }
    }

    if (do_track) tracker_free_result(&res);
    close(sock);
    return 0;
}
