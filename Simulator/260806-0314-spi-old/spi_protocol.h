#ifndef SPI_PROTOCOL_H
#define SPI_PROTOCOL_H

/*
 * ============================================================================
 *  SPI 帧协议 (RK3568 = SPI 主机, 轮询传感器; 传感器 = 从机, 每次返回最新帧)
 * ----------------------------------------------------------------------------
 *  详见 docs/spi-protocol.md。本头文件与传感器侧共用, 传感器端按同样结构填字节。
 *
 *  帧 = 固定 182 字节, 小端序 (ARM 原生):
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
 *  RK3568 每次主机读 182 字节; 传感器应双缓冲, 保证返回一帧完整数据。
 *  若 seq 未变说明传感器还没更新, RK 丢弃该帧。
 * ============================================================================
 */

#include <cstdint>
#include <cstddef>
#include "tracker3d.h"   /* Tracker3DMeasurement */

#define SPI_FRAME_MAGIC0   0xA5
#define SPI_FRAME_MAGIC1   0x5A
#define SPI_MAX_ANCHORS    TRACKER3D_MAX_ANCHORS   /* 8 */

#pragma pack(push, 1)
typedef struct {
    uint8_t  has;          /* bit0 tdoa | bit1 toa | bit2 aoa | bit3 rss */
    float    tdoa_sec;     /* 有效当 has&1 */
    float    toa_sec;      /* 有效当 has&2 */
    float    aoa_az;       /* 有效当 has&4 */
    float    aoa_el;       /* 有效当 has&4 */
    float    rss_dbm;      /* 有效当 has&8 */
} SpiAnchor;               /* 21 字节 */

typedef struct {
    uint8_t   magic[2];    /* {0xA5, 0x5A} */
    uint16_t  seq;
    uint8_t   mode_mask;
    uint8_t   n_anc;
    uint32_t  dt_us;
    uint16_t  reserved;     /* 0 */
    SpiAnchor anchors[SPI_MAX_ANCHORS];
    uint16_t  crc16;        /* CRC-16/CCITT-FALSE over bytes [0..179] */
} SpiFrame;                 /* 182 字节 */
#pragma pack(pop)

static_assert(sizeof(SpiAnchor) == 21, "SpiAnchor must be 21 bytes");
static_assert(sizeof(SpiFrame)  == 182, "SpiFrame must be 182 bytes");

/* 模态位定义 (与 mode_mask / has 共用) */
#define SPI_MODE_TDOA  (1u << 0)
#define SPI_MODE_TOA   (1u << 1)
#define SPI_MODE_AOA   (1u << 2)
#define SPI_MODE_RSS   (1u << 3)

/* CRC-16/CCITT-FALSE: poly=0x1021, init=0xFFFF, refIn=false, refOut=false, xorOut=0 */
static inline uint16_t spi_crc16_ccitt(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; ++b)
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u) : (uint16_t)(crc << 1);
    }
    return crc;
}

/* 校验帧: 魔数 + CRC。返回 0=有效, 非 0=无效 */
static inline int spi_frame_valid(const SpiFrame *f) {
    if (f->magic[0] != SPI_FRAME_MAGIC0 || f->magic[1] != SPI_FRAME_MAGIC1)
        return 1;
    uint16_t crc = spi_crc16_ccitt((const uint8_t *)f, offsetof(SpiFrame, crc16));
    return (crc == f->crc16) ? 0 : 2;
}

/* SpiFrame -> Tracker3DMeasurement (按 has 位填, 未测的 has_* 保持 0) */
static inline void spi_frame_to_measurement(const SpiFrame *f, Tracker3DMeasurement *m) {
    tracker3d_clear_measurement(m);
    for (int i = 0; i < SPI_MAX_ANCHORS; ++i) {
        uint8_t has = f->anchors[i].has;
        if (has & SPI_MODE_TDOA) { m->has_tdoa[i] = 1; m->tdoa[i] = f->anchors[i].tdoa_sec; }
        if (has & SPI_MODE_TOA)  { m->has_toa[i]  = 1; m->toa[i]  = f->anchors[i].toa_sec;  }
        if (has & SPI_MODE_AOA)  { m->has_aoa[i]  = 1; m->aoa_az[i] = f->anchors[i].aoa_az;
                                                              m->aoa_el[i] = f->anchors[i].aoa_el; }
        if (has & SPI_MODE_RSS)  { m->has_rss[i]  = 1; m->rss[i]  = f->anchors[i].rss_dbm; }
    }
}

#endif /* SPI_PROTOCOL_H */
