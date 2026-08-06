/**
  ******************************************************************************
  * @file    spi_protocol.h
  * @brief   RK3568<->F407 SPI 帧协议定义（与 F4 固件 bsp_spi/proto 逐字节一致）
  *          本协议同时也被 260806-0232- 完备 的 Qt 工程复用（SpiFrame/SpiAnchor）。
  ******************************************************************************
  */
#ifndef SPI_PROTOCOL_H
#define SPI_PROTOCOL_H

#include <stdint.h>

/* ---- SPI 帧 magic / 尺寸 ---- */
#define SPI_MAGIC0       0xA5
#define SPI_MAGIC1       0x5A
#define SPI_MAX_ANCHORS  8
#define SPI_ANCHOR_LEN   21
#define SPI_FRAME_LEN    182      /* magic(2) + hdr(10) + 8*21 + crc(2) */

/* ---- 测量模式位 ---- */
#define SPI_MODE_TDOA    (1u << 0)
#define SPI_MODE_TOA     (1u << 1)
#define SPI_MODE_AOA     (1u << 2)
#define SPI_MODE_RSS     (1u << 3)

/* ---- 同步握手字节（F4 上电未就绪时首字节会回 0xA5，就绪回 0x00）---- */
#define SPI_SYNC_BUSY    0xA5
#define SPI_SYNC_READY   0x00

#pragma pack(push, 1)

typedef struct {
    uint8_t  has;        /* 该锚点有效测量模式位掩码 */
    float    tdoa_sec;   /* 相对参考锚点的 TDOA (秒) */
    float    toa_sec;    /* 绝对 TOA (秒) */
    float    aoa_az;     /* 方位角 (弧度) */
    float    aoa_el;     /* 俯仰角 (弧度) */
    float    rss_dbm;    /* 接收信号强度 (dBm) */
} SpiAnchor;

typedef struct {
    uint16_t magic;      /* 0xA55A */
    uint16_t seq;        /* 帧序号 */
    uint8_t  mode_mask;  /* 全局使能模式位 */
    uint8_t  n_anc;      /* 有效锚点数量 */
    uint32_t dt_us;      /* 距离上一帧的时间差 (us) */
    uint16_t reserved;
    SpiAnchor anchors[SPI_MAX_ANCHORS];
    uint16_t crc;        /* CRC16-CCITT-FALSE，覆盖前 180 字节 */
} SpiFrame;

#pragma pack(pop)

/* CRC-16/CCITT-FALSE（与 F4 固件、Host/protocol.py 一致）*/
static inline uint16_t spi_crc16(const uint8_t *data, uint32_t len)
{
    uint16_t crc = 0xFFFF;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    return crc;
}

static inline int spi_frame_validate(const SpiFrame *f)
{
    if (f->magic != (uint16_t)(SPI_MAGIC0 | (SPI_MAGIC1 << 8))) return -1;
    if (spi_crc16((const uint8_t *)f, SPI_FRAME_LEN - 2) != f->crc) return -2;
    return 0;
}

#endif /* SPI_PROTOCOL_H */
