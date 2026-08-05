/**
  ******************************************************************************
  * @file    proto.h
  * @brief   协议定义
  *
  *  两条链路：
  *   (1) UART (PC 上位机控制 + 遥测): 可变长帧
  *        SYNC0 SYNC1 | TYPE | LEN(2) | SEQ(2) | TS(4) | PAYLOAD | CRC16(2)
  *   (2) SPI (F407 从机 -> RK3568 主机): 固定 182 字节 SpiFrame（见下）
  *
  *  UART 的 DATA_FRAME 载荷 = 一整个 SpiFrame（182B），便于上位机观察 SPI 链路内容。
  *  UART 的 TRUTH_FRAME 载荷 = 目标真实位置 (x,y,z)，仅供上位机绘图。
  ******************************************************************************
  */
#ifndef __PROTO_H
#define __PROTO_H

#include <stdint.h>
#include <stddef.h>

/* ============================ UART 帧定界 ============================ */
#define PROTO_SYNC0                 0xA5
#define PROTO_SYNC1                 0x5A

#define PROTO_HDR_LEN               9u    /* TYPE(1)+LEN(2)+SEQ(2)+TS(4) */
#define PROTO_CRC_LEN               2u
#define PROTO_OVERHEAD              (PROTO_HDR_LEN + PROTO_CRC_LEN)
#define PROTO_MAX_PAYLOAD           256u
#define PROTO_MAX_FRAME_LEN         (2u + PROTO_OVERHEAD + PROTO_MAX_PAYLOAD)

/* ============================ 帧类型 ============================ */
typedef enum
{
    /* PC -> F407 */
    PROTO_CMD_PING         = 0x01,
    PROTO_CMD_SET_CONFIG   = 0x02,
    PROTO_CMD_START        = 0x03,
    PROTO_CMD_STOP         = 0x04,
    PROTO_CMD_RESET_SEQ    = 0x05,
    PROTO_CMD_SET_RATE     = 0x06,
    PROTO_CMD_GET_STATUS   = 0x07,

    /* F407 -> PC */
    PROTO_RSP_PONG         = 0x81,
    PROTO_RSP_ACK          = 0x82,
    PROTO_RSP_NACK          = 0x83,
    PROTO_RSP_STATUS       = 0x84,
    PROTO_RSP_DATA_FRAME   = 0x85,   /* 载荷 = SpiFrame (182B) */
    PROTO_RSP_TRUTH        = 0x87,   /* 载荷 = 真实位置 x,y,z (float) */
    PROTO_RSP_LOG          = 0x86,
} ProtoType_e;

typedef enum
{
    PROTO_STAT_OK              = 0x00,
    PROTO_STAT_ERR_LEN        = 0x01,
    PROTO_STAT_ERR_CRC        = 0x02,
    PROTO_STAT_ERR_PARAM      = 0x03,
    PROTO_STAT_ERR_BUSY       = 0x04,
    PROTO_STAT_ERR_NOT_RUNNING= 0x05,
    PROTO_STAT_ERR_UNSUPPORTED= 0x06,
} ProtoStat_e;

/* ============================ SPI 帧 (182B, 与 RK3568 spi_protocol.h 一致) ============================ */
#define SPI_FRAME_MAGIC0   0xA5
#define SPI_FRAME_MAGIC1   0x5A
#define SPI_MAX_ANCHORS    8u
#define SPI_FRAME_LEN      182u
#define SPI_ANCHOR_LEN     21u

/* 模态位 (mode_mask / has 共用) */
#define SPI_MODE_TDOA  (1u << 0)
#define SPI_MODE_TOA   (1u << 1)
#define SPI_MODE_AOA   (1u << 2)
#define SPI_MODE_RSS   (1u << 3)

typedef struct __attribute__((packed))
{
    uint8_t  has;          /* bit0 tdoa | bit1 toa | bit2 aoa | bit3 rss */
    float    tdoa_sec;     /* 秒 (has&1) */
    float    toa_sec;      /* 秒 (has&2) */
    float    aoa_az;       /* 弧度 (has&4) */
    float    aoa_el;       /* 弧度 (has&4) */
    float    rss_dbm;      /* dBm (has&8) */
} SpiAnchor_t;             /* 21 字节 */

typedef struct __attribute__((packed))
{
    uint8_t      magic[2];   /* {0xA5, 0x5A} */
    uint16_t     seq;
    uint8_t      mode_mask;
    uint8_t      n_anc;
    uint32_t     dt_us;
    uint16_t     reserved;   /* 0 */
    SpiAnchor_t  anchors[SPI_MAX_ANCHORS];
    uint16_t     crc16;      /* CRC-16/CCITT-FALSE over [0..179] */
} SpiFrame_t;                /* 182 字节 */

/* ============================ 配置 (PC -> F407) ============================ */
/* 场景 */
#define SIM_SCENE_STRAIGHT  0u
#define SIM_SCENE_CLIMB     1u
#define SIM_SCENE_TURN      2u
#define SIM_SCENE_CUSTOM    3u

typedef struct __attribute__((packed))
{
    uint32_t sample_rate_hz;     /* 帧生成率 1..1000 Hz */
    uint8_t  scene;             /* SIM_SCENE_* */
    uint8_t  enable_mask;       /* bit0 tdoa,1 toa,2 aoa,3 rss */
    uint8_t  telemetry_decim;   /* UART 遥测抽稀 */
    uint8_t  reserved;
    float    init_x, init_y, init_z;   /* scene=CUSTOM 时初始位置 (m) */
    float    vel_x,  vel_y,  vel_z;    /* scene=CUSTOM 时速度 (m/s) */
} ProtoConfig_t;   /* 4+1+1+1+1 + 12 + 12 = 32 字节 */

/* ============================ 状态帧 ============================ */
typedef struct __attribute__((packed))
{
    uint8_t  running;
    uint8_t  scene;
    uint8_t  enable_mask;
    uint8_t  reserved;
    uint32_t sample_rate_hz;
    uint32_t frame_count;      /* 累计生成帧数 */
    uint32_t spi_xfer;         /* SPI 完成的读取次数 (DMA TC) */
    uint32_t uart_overrun;
    uint32_t crc_errors;
    uint32_t cmd_count;
} ProtoStatus_t;

/* ============================ 真实位置帧 (UART only) ============================ */
typedef struct __attribute__((packed))
{
    float    x, y, z;          /* 目标真实位置 (m) */
    float    vx, vy, vz;      /* 速度 (m/s) */
} ProtoTruth_t;

/* ============================ API ============================ */
uint16_t Proto_CRC16(const uint8_t *data, uint32_t len);
uint16_t Proto_BuildFrame(uint8_t *out, uint8_t type, uint16_t seq,
                          uint32_t ts_ms, const uint8_t *payload, uint16_t plen);
/* 计算 SpiFrame 的 CRC 并填入；返回 CRC */
uint16_t SpiFrame_Finalize(SpiFrame_t *f);

#endif /* __PROTO_H */
