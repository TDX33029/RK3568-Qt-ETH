/**
  ******************************************************************************
  * @file    proto.c
  * @brief   协议帧组包 + CRC16 + SpiFrame 定稿
  ******************************************************************************
  */
#include "proto.h"

/* CRC-16/CCITT-FALSE: 与 RK3568 端 spi_crc16_ccitt 完全一致 */
uint16_t Proto_CRC16(const uint8_t *data, uint32_t len)
{
    uint16_t crc = 0xFFFFu;
    uint32_t i;
    uint8_t b;

    for (i = 0; i < len; i++)
    {
        crc ^= (uint16_t)data[i] << 8;
        for (b = 0; b < 8; b++)
        {
            if (crc & 0x8000u)
                crc = (uint16_t)((crc << 1) ^ 0x1021u);
            else
                crc = (uint16_t)(crc << 1);
        }
    }
    return crc;
}

uint16_t Proto_BuildFrame(uint8_t *out, uint8_t type, uint16_t seq,
                          uint32_t ts_ms, const uint8_t *payload, uint16_t plen)
{
    uint16_t crc;
    uint16_t off = 0;
    uint16_t i;

    if (out == NULL) return 0;
    if (plen > PROTO_MAX_PAYLOAD) return 0;
    if (plen > 0 && payload == NULL) return 0;

    out[off++] = PROTO_SYNC0;
    out[off++] = PROTO_SYNC1;
    out[off++] = type;
    out[off++] = (uint8_t)(plen & 0xFF);
    out[off++] = (uint8_t)((plen >> 8) & 0xFF);
    out[off++] = (uint8_t)(seq & 0xFF);
    out[off++] = (uint8_t)((seq >> 8) & 0xFF);
    out[off++] = (uint8_t)(ts_ms & 0xFF);
    out[off++] = (uint8_t)((ts_ms >> 8) & 0xFF);
    out[off++] = (uint8_t)((ts_ms >> 16) & 0xFF);
    out[off++] = (uint8_t)((ts_ms >> 24) & 0xFF);

    for (i = 0; i < plen; i++)
        out[off++] = payload[i];

    /* CRC 覆盖 TYPE..PAYLOAD = 头(9) + 载荷(plen) */
    crc = Proto_CRC16(&out[2], (uint32_t)PROTO_HDR_LEN + plen);
    out[off++] = (uint8_t)(crc & 0xFF);
    out[off++] = (uint8_t)((crc >> 8) & 0xFF);

    return off;
}

/* 计算 SpiFrame 的 CRC (覆盖 [0..179], 即 magic..anchors 末) 并写入 crc16 字段 */
uint16_t SpiFrame_Finalize(SpiFrame_t *f)
{
    uint16_t crc;
    crc = Proto_CRC16((const uint8_t *)f, SPI_FRAME_LEN - PROTO_CRC_LEN); /* [0..179] */
    f->crc16 = crc;
    return crc;
}
