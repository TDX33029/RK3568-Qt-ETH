#ifndef FRAME_PROTOCOL_H
#define FRAME_PROTOCOL_H

/*
 * ============================================================================
 *  测量帧协议 (RK3568 通过 ETH1 / UDP 接收, 每个数据报恰好为一帧)
 * ----------------------------------------------------------------------------
 *  详见 docs/frame-protocol.md。本头文件与传感器侧共用, 传感器端按同样结构填字节。
 *
 *  帧 = 固定 182 字节, 小端序 (ARM 原生), 经 UDP 数据报传输:
 *    [ 0..1 ]  magic      = {0xA5, 0x5A}        帧同步
 *    [ 2..3 ]  seq        uint16                帧序号(回绕), 用于去重/丢帧检测
 *    [ 4   ]  mode_mask   uint8                 传感器提供的模态位图
 *                            bit0 TDOA  bit1 TOA  bit2 AOA  bit3 RSS
 *    [ 5   ]  n_anc      uint8                 实际有数据的锚点数 (信息用, 始终传 8 个槽)
 *    [ 6..9 ]  dt_us      uint32                距上一帧的微秒数 (用于 EKF 预测)
 *    [10..11]  reserved   uint16 = 0
 *    [12..179] anchors[8] 每个 21 字节:
 *          has      uint8    bit0 tdoa,1 toa,2 aoa,3 rss  (本锚点哪些有效)
 *          tdoa_sec float32  (秒)
 *          toa_sec  float32  (秒)
 *          aoa_az   float32  (弧度)
 *          aoa_el   float32  (弧度)
 *          rss_dbm  float32  (dBm)
 *    [180..181] crc16  uint16  CRC-16/CCITT-FALSE, 覆盖字节 [0..179]
 *
 *  注: 本项目不使用 SPI, 数据链路完全走 ETH1/UDP, 板子无任何 SPI 引脚操作。
 *  若 UDP 数据报长度与 182 字节不符, 接收端直接丢弃。
 *  seq 未变说明传感器还没更新, 接收端丢弃该帧。
 * ============================================================================
 */

#include <cstdint>
#include <cstddef>
#include <cstring>       /* memcpy (udp_ping_to_pong) */
#include "tracker3d.h"   /* Tracker3DMeasurement */

#define UDP_FRAME_MAGIC0   0xA5
#define UDP_FRAME_MAGIC1   0x5A
#define UDP_MAX_ANCHORS    TRACKER3D_MAX_ANCHORS   /* 8 */

#pragma pack(push, 1)
typedef struct {
    uint8_t  has;          /* bit0 tdoa | bit1 toa | bit2 aoa | bit3 rss */
    float    tdoa_sec;     /* 有效当 has&1 */
    float    toa_sec;      /* 有效当 has&2 */
    float    aoa_az;       /* 有效当 has&4 */
    float    aoa_el;       /* 有效当 has&4 */
    float    rss_dbm;      /* 有效当 has&8 */
} UdpAnchor;               /* 21 字节 */

typedef struct {
    uint8_t   magic[2];    /* {0xA5, 0x5A} */
    uint16_t  seq;
    uint8_t   mode_mask;
    uint8_t   n_anc;
    uint32_t  dt_us;
    uint16_t  reserved;     /* 0 */
    UdpAnchor anchors[UDP_MAX_ANCHORS];
    uint16_t  crc16;        /* CRC-16/CCITT-FALSE over bytes [0..179] */
} UdpFrame;                 /* 182 字节 */
#pragma pack(pop)

static_assert(sizeof(UdpAnchor) == 21, "UdpAnchor must be 21 bytes");
static_assert(sizeof(UdpFrame)  == 182, "UdpFrame must be 182 bytes");

/* 模态位定义 (与 mode_mask / has 共用) */
#define UDP_MODE_TDOA  (1u << 0)
#define UDP_MODE_TOA   (1u << 1)
#define UDP_MODE_AOA   (1u << 2)
#define UDP_MODE_RSS   (1u << 3)

/* CRC-16/CCITT-FALSE: poly=0x1021, init=0xFFFF, refIn=false, refOut=false, xorOut=0 */
static inline uint16_t udp_crc16_ccitt(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; ++b)
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u) : (uint16_t)(crc << 1);
    }
    return crc;
}

/* 校验帧: 魔数 + CRC。返回 0=有效, 非 0=无效 */
static inline int udp_frame_valid(const UdpFrame *f) {
    if (f->magic[0] != UDP_FRAME_MAGIC0 || f->magic[1] != UDP_FRAME_MAGIC1)
        return 1;
    uint16_t crc = udp_crc16_ccitt((const uint8_t *)f, offsetof(UdpFrame, crc16));
    return (crc == f->crc16) ? 0 : 2;
}

/* ── UDP 探测帧 (联通测试): 8 字节, 与 182B 数据帧区分 ───────────────
 *   [0..1] magic {0xA5,0x5A}
 *   [2..5] "PING" (上位机 -> 板子) / "PONG" (板子回)
 *   [6]    seq (u8, 回显, 供上位机计算 RTT)
 *   [7]    0
 * 板端收到 PING 即回 PONG (无需走 182B 帧校验路径), 上位机据此确认
 * 板端 UDP 接收链路双向可用。 */
#define UDP_PING_LEN 8

static inline int udp_ping_match(const uint8_t *p, size_t n) {
    return n == UDP_PING_LEN && p[0] == UDP_FRAME_MAGIC0 && p[1] == UDP_FRAME_MAGIC1
        && p[2] == 'P' && p[3] == 'I' && p[4] == 'N' && p[5] == 'G';
}

static inline void udp_ping_to_pong(const uint8_t *ping, uint8_t *pong) {
    memcpy(pong, ping, UDP_PING_LEN);
    pong[2] = 'P'; pong[3] = 'O'; pong[4] = 'N'; pong[5] = 'G';
    pong[7] = 0;
}

/* ── 基站配置帧 (上位机 -> 板子): 106 字节, 与 182B 数据帧区分 ──────
 *   [0..1]   magic {0xA5,0x5A}
 *   [2..5]   "CFG"
 *   [6]      seq (u8, 回显到 ACK)
 *   [7]      n_anc (u8, 有效锚点数)
 *   [8..103] anchors[n_anc] {x,y,z float32}, 每锚点 12B (n_anc<=8)
 *   [104..105] crc16 (覆盖 [0..103])
 * 板端收到即更新 EKF 锚点坐标并回 8B ACK 帧:
 *   {magic, 'A','C','K', seq, 0}                                          */
#define UDP_CFG_LEN 106

typedef struct {
    float x;
    float y;
    float z;
} UdpCfgAnchor;               /* 12 字节 */

static inline int udp_cfg_match(const uint8_t *p, size_t n) {
    return n == UDP_CFG_LEN && p[0] == UDP_FRAME_MAGIC0 && p[1] == UDP_FRAME_MAGIC1
        && p[2] == 'C' && p[3] == 'F' && p[4] == 'G';
}

/* 解析 CFG: 返回有效锚点数 (-1=CRC 错误)。out 容量 >= max_n。 */
static inline int udp_cfg_parse(const uint8_t *p, UdpCfgAnchor *out, int max_n) {
    uint16_t crc = udp_crc16_ccitt(p, 104);
    uint16_t recv = (uint16_t)(p[104] | (p[105] << 8));
    if (crc != recv)
        return -1;
    int n = p[7];
    if (n < 0) n = 0;
    if (n > max_n) n = max_n;
    for (int i = 0; i < n; ++i)
        memcpy(&out[i], p + 8 + i * 12, sizeof(UdpCfgAnchor));
    return n;
}

static inline int udp_ack_match(const uint8_t *p, size_t n) {
    return n == UDP_PING_LEN && p[0] == UDP_FRAME_MAGIC0 && p[1] == UDP_FRAME_MAGIC1
        && p[2] == 'A' && p[3] == 'C' && p[4] == 'K';
}

static inline void udp_cfg_to_ack(const uint8_t *cfg, uint8_t *ack) {
    memcpy(ack, cfg, UDP_PING_LEN);
    ack[2] = 'A'; ack[3] = 'C'; ack[4] = 'K';   /* ACK, seq 回显 */
    ack[7] = 0;
}

/* UdpFrame -> Tracker3DMeasurement (按 has 位填, 未测的 has_* 保持 0) */
static inline void udp_frame_to_measurement(const UdpFrame *f, Tracker3DMeasurement *m) {
    tracker3d_clear_measurement(m);
    for (int i = 0; i < UDP_MAX_ANCHORS; ++i) {
        uint8_t has = f->anchors[i].has;
        if (has & UDP_MODE_TDOA) { m->has_tdoa[i] = 1; m->tdoa[i] = f->anchors[i].tdoa_sec; }
        if (has & UDP_MODE_TOA)  { m->has_toa[i]  = 1; m->toa[i]  = f->anchors[i].toa_sec;  }
        if (has & UDP_MODE_AOA)  { m->has_aoa[i]  = 1; m->aoa_az[i] = f->anchors[i].aoa_az;
                                                              m->aoa_el[i] = f->anchors[i].aoa_el; }
        if (has & UDP_MODE_RSS)  { m->has_rss[i]  = 1; m->rss[i]  = f->anchors[i].rss_dbm; }
    }
}

#endif /* FRAME_PROTOCOL_H */
