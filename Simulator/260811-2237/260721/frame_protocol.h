#ifndef FRAME_PROTOCOL_H
#define FRAME_PROTOCOL_H

#include <cstdint>
#include <cstddef>
#include <cstring>
#include "alg/tracker3d.h"

#define UDP_FRAME_MAGIC0   0xA5
#define UDP_FRAME_MAGIC1   0x5A
#define UDP_MAX_ANCHORS    TRACKER3D_MAX_ANCHORS

#pragma pack(push, 1)
typedef struct {
    uint8_t  has;
    float    tdoa_sec;
    float    toa_sec;
    float    aoa_az;
    float    aoa_el;
    float    rss_dbm;
} UdpAnchor;

typedef struct {
    uint8_t   magic[2];
    uint16_t  seq;
    uint8_t   mode_mask;
    uint8_t   n_anc;
    uint32_t  dt_us;
    uint16_t  reserved;
    UdpAnchor anchors[UDP_MAX_ANCHORS];
    uint16_t  crc16;
} UdpFrame;
#pragma pack(pop)

static_assert(sizeof(UdpAnchor) == 21, "UdpAnchor must be 21 bytes");
static_assert(sizeof(UdpFrame)  == 182, "UdpFrame must be 182 bytes");

#define UDP_MODE_TDOA  (1u << 0)
#define UDP_MODE_TOA   (1u << 1)
#define UDP_MODE_AOA   (1u << 2)
#define UDP_MODE_RSS   (1u << 3)

static inline uint16_t udp_crc16_ccitt(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; ++b)
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u) : (uint16_t)(crc << 1);
    }
    return crc;
}

static inline int udp_frame_valid(const UdpFrame *f) {
    if (f->magic[0] != UDP_FRAME_MAGIC0 || f->magic[1] != UDP_FRAME_MAGIC1)
        return 1;
    uint16_t crc = udp_crc16_ccitt((const uint8_t *)f, offsetof(UdpFrame, crc16));
    return (crc == f->crc16) ? 0 : 2;
}

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

#define UDP_CFG_LEN 106

typedef struct {
    float x;
    float y;
    float z;
} UdpCfgAnchor;

static inline int udp_cfg_match(const uint8_t *p, size_t n) {
    return n == UDP_CFG_LEN && p[0] == UDP_FRAME_MAGIC0 && p[1] == UDP_FRAME_MAGIC1
        && p[2] == 'C' && p[3] == 'F' && p[4] == 'G';
}

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
    ack[2] = 'A'; ack[3] = 'C'; ack[4] = 'K';
    ack[7] = 0;
}

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

#endif
