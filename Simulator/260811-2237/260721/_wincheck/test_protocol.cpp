/* 协议/统计逻辑验证 (仅本机, 不部署) —— 覆盖 frame_protocol 与 ethtest 丢帧逻辑 */
#include "frame_protocol.h"
#include <cstdio>
#include <cstring>
#include <cstdint>

static int fails = 0;
#define CHECK(cond, msg) do { if (cond) printf("PASS: %s\n", msg); \
        else { printf("FAIL: %s\n", msg); ++fails; } } while (0)

/* ethtest.cpp 内联的丢帧统计逻辑 (保持与其一致) */
static int compute_dropped(uint32_t seq_prev, uint16_t seq) {
    uint16_t prev_seq = (seq_prev == 0xFFFFFFFFu) ? 0xFFFFu : (uint16_t)seq_prev;
    int dropped = 0;
    if (prev_seq != 0xFFFFu) {
        uint16_t d = (uint16_t)((int)seq - (int)prev_seq);
        if (d >= 2 && d < 32768) dropped = (int)(d - 1);
    }
    return dropped;
}

int main() {
    /* 1. 合法帧通过校验 */
    UdpFrame f;
    memset(&f, 0, sizeof(f));
    f.magic[0] = 0xA5; f.magic[1] = 0x5A;
    f.seq = 7; f.mode_mask = UDP_MODE_TDOA; f.n_anc = 5; f.dt_us = 10000;
    for (int i = 0; i < 5; ++i) { f.anchors[i].has = UDP_MODE_TDOA; f.anchors[i].tdoa_sec = 0.001f * (i + 1); }
    f.crc16 = udp_crc16_ccitt((const uint8_t *)&f, offsetof(UdpFrame, crc16));
    CHECK(udp_frame_valid(&f) == 0, "合法帧 CRC/魔数校验通过");

    /* 2. 魔数错误 */
    UdpFrame b = f; b.magic[0] = 0x00;
    CHECK(udp_frame_valid(&b) == 1, "魔数错误被拒 (rc=1)");

    /* 3. CRC 错误 */
    UdpFrame c = f; c.crc16 ^= 0xFFFF;
    CHECK(udp_frame_valid(&c) == 2, "CRC 错误被拒 (rc=2)");

    /* 4. 帧长静态断言 (编译期已校验, 这里运行时再确认) */
    CHECK(sizeof(UdpFrame) == 182 && sizeof(UdpAnchor) == 21, "帧长 182 / 锚点 21");

    /* 5. udp_frame_to_measurement 位映射 */
    UdpFrame d;
    memset(&d, 0, sizeof(d));
    d.anchors[0].has = UDP_MODE_TDOA | UDP_MODE_AOA;
    d.anchors[0].tdoa_sec = 0.25f; d.anchors[0].aoa_az = 0.5f; d.anchors[0].aoa_el = -0.3f;
    d.anchors[3].has = UDP_MODE_RSS; d.anchors[3].rss_dbm = -72.0f;
    Tracker3DMeasurement m;
    udp_frame_to_measurement(&d, &m);
    CHECK(m.has_tdoa[0] == 1 && m.tdoa[0] == 0.25, "TDOA 映射正确");
    CHECK(m.has_aoa[0] == 1 && m.aoa_az[0] == 0.5f && m.aoa_el[0] == -0.3f, "AOA 映射正确");
    CHECK(m.has_rss[3] == 1 && m.rss[3] == -72.0f, "RSS 映射正确");
    CHECK(m.has_tdoa[1] == 0 && m.has_rss[0] == 0, "未声明模态保持 0");

    /* 6. ethtest 丢帧统计: 首帧 / 连续 / 跳变 / 同帧重发 / 乱序 */
    CHECK(compute_dropped(0xFFFFFFFFu, 0) == 0, "首帧不计丢帧");
    CHECK(compute_dropped(100, 101) == 0, "连续帧不计丢帧");
    CHECK(compute_dropped(100, 103) == 2, "跳变计丢帧=2");
    CHECK(compute_dropped(100, 100) == 0, "同帧重发不计丢帧(旧版会算 -1)");
    CHECK(compute_dropped(100, 99) == 0, "乱序/回绕不计丢帧(旧版会算 65533)");
    CHECK(compute_dropped(0xFFFF, 0) == 0, "seq 回绕 0xFFFF->0 不计丢帧");

    printf(fails ? "\n%d 项失败\n" : "\n全部 15 项通过\n", fails);
    return fails ? 1 : 0;
}
