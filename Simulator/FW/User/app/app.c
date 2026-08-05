/**
  ******************************************************************************
  * @file    app.c
  * @brief   应用层：UART 命令解析/分发 + 仿真主循环 + 遥测
  *          SPI 从机始终在线（DMA 自行响应 RK 轮询）；运行态由 TIM2 驱动 SIM_Step。
  ******************************************************************************
  */
#include "stm32f4xx.h"
#include "proto.h"
#include "bsp_clock.h"
#include "bsp_uart.h"
#include "bsp_spi.h"
#include "bsp_timer.h"
#include "sim.h"
#include "../beep/bsp_beep.h"
#include "app.h"

#define APP_BEEP_ENABLE   1

/* ----------------------- 状态 ----------------------- */
static ProtoConfig_t s_cfg;
static volatile uint8_t  s_running = 0;

static uint32_t s_frame_count  = 0;
static uint32_t s_uart_overrun = 0;
static uint32_t s_crc_errors   = 0;
static uint32_t s_cmd_count     = 0;
static uint16_t s_data_seq      = 0;   /* UART 帧序号 */
static uint16_t s_telemetry_skip= 0;

/* ----------------------- 默认配置 ------------------- */
static void cfg_load_default(ProtoConfig_t *c)
{
    c->sample_rate_hz   = 100u;
    c->scene            = SIM_SCENE_STRAIGHT;
    c->enable_mask      = SPI_MODE_TDOA | SPI_MODE_TOA | SPI_MODE_AOA | SPI_MODE_RSS;
    c->telemetry_decim  = 10u;          /* SpiFrame 快照抽稀（truth 每帧都发） */
    c->reserved         = 0;
    c->init_x = 8.0f;  c->init_y = 10.0f; c->init_z = 6.0f;
    c->vel_x  = 1.20f; c->vel_y  = 0.75f; c->vel_z  = 0.20f;
}

/* ----------------------- 发送辅助 ------------------- */
static void send_frame(uint8_t type, const uint8_t *payload, uint16_t plen)
{
    static uint8_t outbuf[PROTO_MAX_FRAME_LEN];
    uint16_t n = Proto_BuildFrame(outbuf, type, s_data_seq++, BSP_Millis(), payload, plen);
    if (n > 0)
    {
        BSP_UART_Send(outbuf, n);
        if (BSP_UART_TX_Avail() > (1024u - PROTO_MAX_FRAME_LEN))
            s_uart_overrun++;
    }
}

static void send_ack(uint8_t cmd_type, uint8_t stat)
{
    uint8_t p[2] = { cmd_type, stat };
    static uint8_t outbuf[PROTO_MAX_FRAME_LEN];
    uint16_t n = Proto_BuildFrame(outbuf,
                                  (stat == PROTO_STAT_OK) ? PROTO_RSP_ACK : PROTO_RSP_NACK,
                                  0, BSP_Millis(), p, sizeof(p));
    if (n > 0) BSP_UART_Send(outbuf, n);
}

static void send_status(void)
{
    ProtoStatus_t st;
    static uint8_t outbuf[PROTO_MAX_FRAME_LEN];
    uint16_t n;
    st.running        = s_running;
    st.scene          = s_cfg.scene;
    st.enable_mask    = s_cfg.enable_mask;
    st.reserved       = 0;
    st.sample_rate_hz = s_cfg.sample_rate_hz;
    st.frame_count    = s_frame_count;
    st.spi_xfer       = BSP_SPI_GetXferCount();
    st.uart_overrun   = s_uart_overrun;
    st.crc_errors     = s_crc_errors;
    st.cmd_count      = s_cmd_count;
    n = Proto_BuildFrame(outbuf, PROTO_RSP_STATUS, 0, BSP_Millis(),
                         (const uint8_t *)&st, sizeof(st));
    if (n > 0) BSP_UART_Send(outbuf, n);
}

/* ----------------------- 命令分发 ------------------- */
static void dispatch_cmd(uint8_t type, const uint8_t *payload, uint16_t len)
{
    uint32_t hz;
    uint16_t i;
    s_cmd_count++;

    switch (type)
    {
        case PROTO_CMD_PING:
            send_frame(PROTO_RSP_PONG, (const uint8_t *)"F407-SIM", 8);
            break;

        case PROTO_CMD_SET_CONFIG:
            if (len != sizeof(ProtoConfig_t)) { send_ack(type, PROTO_STAT_ERR_LEN); break; }
            {
                ProtoConfig_t nc;
                for (i = 0; i < sizeof(ProtoConfig_t); i++) ((uint8_t *)&nc)[i] = payload[i];
                if (nc.sample_rate_hz == 0u || nc.sample_rate_hz > 1000u)
                { send_ack(type, PROTO_STAT_ERR_PARAM); break; }
                if (nc.scene > SIM_SCENE_CUSTOM)
                { send_ack(type, PROTO_STAT_ERR_PARAM); break; }
                s_cfg = nc;
                SIM_ApplyConfig(&s_cfg);
                if (s_running) BSP_Timer_SetRate(s_cfg.sample_rate_hz);
                send_ack(type, PROTO_STAT_OK);
            }
            break;

        case PROTO_CMD_SET_RATE:
            if (len != 4u) { send_ack(type, PROTO_STAT_ERR_LEN); break; }
            hz = (uint32_t)payload[0] | ((uint32_t)payload[1] << 8)
               | ((uint32_t)payload[2] << 16) | ((uint32_t)payload[3] << 24);
            if (hz == 0u || hz > 1000u) { send_ack(type, PROTO_STAT_ERR_PARAM); break; }
            s_cfg.sample_rate_hz = hz;
            if (s_running) BSP_Timer_SetRate(hz);
            send_ack(type, PROTO_STAT_OK);
            break;

        case PROTO_CMD_START:
            if (s_running) { send_ack(type, PROTO_STAT_ERR_BUSY); break; }
            SIM_Reset();
            BSP_Timer_SetRate(s_cfg.sample_rate_hz);
            BSP_Timer_Start();
            s_running = 1;
            s_telemetry_skip = 0;
            send_ack(type, PROTO_STAT_OK);
            break;

        case PROTO_CMD_STOP:
            BSP_Timer_Stop();
            s_running = 0;
            send_ack(type, PROTO_STAT_OK);
            break;

        case PROTO_CMD_RESET_SEQ:
            SIM_Reset();
            send_ack(type, PROTO_STAT_OK);
            break;

        case PROTO_CMD_GET_STATUS:
            send_status();
            break;

        default:
            send_ack(type, PROTO_STAT_ERR_UNSUPPORTED);
            break;
    }
}

/* ----------------------- RX 解析状态机 ------------- */
typedef enum { PS_SYNC0, PS_SYNC1, PS_TYPE, PS_LEN0, PS_LEN1,
               PS_SEQ0, PS_SEQ1, PS_TS0, PS_TS1, PS_TS2, PS_TS3,
               PS_PAYLOAD, PS_CRC0, PS_CRC1 } PState_e;

static PState_e ps = PS_SYNC0;
static uint8_t  rx_type;
static uint16_t rx_len;
static uint16_t rx_seq;
static uint32_t rx_ts;
static uint16_t rx_pl_idx;
static uint8_t  rx_payload[PROTO_MAX_PAYLOAD];
static uint16_t rx_crc;
static uint8_t  rx_hdrbuf[PROTO_HDR_LEN];

static void rx_reset(void) { ps = PS_SYNC0; }

static void rx_feed(uint8_t b)
{
    switch (ps)
    {
        case PS_SYNC0: if (b == PROTO_SYNC0) ps = PS_SYNC1; break;
        case PS_SYNC1:
            if (b == PROTO_SYNC1) ps = PS_TYPE;
            else if (b == PROTO_SYNC0) { }
            else ps = PS_SYNC0;
            break;
        case PS_TYPE:  rx_type = b; rx_hdrbuf[0] = b; ps = PS_LEN0; break;
        case PS_LEN0: rx_len = b; rx_hdrbuf[1] = b; ps = PS_LEN1; break;
        case PS_LEN1:
            rx_len |= (uint16_t)b << 8; rx_hdrbuf[2] = b;
            if (rx_len > PROTO_MAX_PAYLOAD) { rx_reset(); break; }
            rx_pl_idx = 0; ps = PS_SEQ0; break;
        case PS_SEQ0: rx_seq = b; rx_hdrbuf[3] = b; ps = PS_SEQ1; break;
        case PS_SEQ1: rx_seq |= (uint16_t)b << 8; rx_hdrbuf[4] = b; ps = PS_TS0; break;
        case PS_TS0:  rx_ts = b; rx_hdrbuf[5] = b; ps = PS_TS1; break;
        case PS_TS1:  rx_ts |= (uint32_t)b << 8; rx_hdrbuf[6] = b; ps = PS_TS2; break;
        case PS_TS2:  rx_ts |= (uint32_t)b << 16; rx_hdrbuf[7] = b; ps = PS_TS3; break;
        case PS_TS3:
            rx_ts |= (uint32_t)b << 24; rx_hdrbuf[8] = b;
            ps = (rx_len > 0) ? PS_PAYLOAD : PS_CRC0; break;
        case PS_PAYLOAD:
            rx_payload[rx_pl_idx++] = b;
            if (rx_pl_idx >= rx_len) ps = PS_CRC0;
            break;
        case PS_CRC0: rx_crc = b; ps = PS_CRC1; break;
        case PS_CRC1:
        {
            static uint8_t tmp[PROTO_HDR_LEN + PROTO_MAX_PAYLOAD];
            uint16_t calc, j;
            rx_crc |= (uint16_t)b << 8;
            for (j = 0; j < sizeof(rx_hdrbuf); j++) tmp[j] = rx_hdrbuf[j];
            for (j = 0; j < rx_len; j++) tmp[sizeof(rx_hdrbuf) + j] = rx_payload[j];
            calc = Proto_CRC16(tmp, (uint32_t)(sizeof(rx_hdrbuf) + rx_len));
            if (calc != rx_crc)
            {
                s_crc_errors++;
#if APP_BEEP_ENABLE
                BEEP_TOGGLE;
#endif
                rx_reset();
            }
            else
            {
                dispatch_cmd(rx_type, rx_payload, rx_len);
                rx_reset();
            }
            break;
        }
        default: rx_reset(); break;
    }
}

/* ----------------------- 主循环步进 ----------------- */
static void process_running(void)
{
    uint16_t req = BSP_Timer_TakeRequests();
    if (req == 0) return;
    if (req > 1) req = 1;   /* 跟不上只处理最新一次 */

    SIM_Step(BSP_Micros());
    s_frame_count++;

    /* TRUTH 每帧回传（小，12B），上位机可丝滑绘图 */
    {
        ProtoTruth_t tr;
        SIM_GetTruth(&tr);
        send_frame(PROTO_RSP_TRUTH, (const uint8_t *)&tr, sizeof(tr));
    }

    /* SpiFrame 快照抽稀（大，182B） */
    if (s_cfg.telemetry_decim > 0u)
    {
        if (s_telemetry_skip == 0u)
        {
            SpiFrame_t *f = SIM_GetLiveFrame();
            send_frame(PROTO_RSP_DATA_FRAME, (const uint8_t *)f, SPI_FRAME_LEN);
            s_telemetry_skip = s_cfg.telemetry_decim - 1u;
        }
        else
        {
            s_telemetry_skip--;
        }
    }
}

void APP_Step(void)
{
    uint8_t b;
    while (BSP_UART_GetByte(&b)) rx_feed(b);
    if (s_running) process_running();
}

void APP_Init(void)
{
    cfg_load_default(&s_cfg);
    SIM_Init();
    SIM_ApplyConfig(&s_cfg);
    rx_reset();
    BSP_SPI_Arm();          /* SPI 从机 DMA 装载初始帧，开始响应 RK 轮询 */

#if APP_BEEP_ENABLE
    BEEP_GPIO_Config();
    BEEP_ON;  BSP_DelayMs(40);  BEEP_OFF;
    BSP_DelayMs(60);
    BEEP_ON;  BSP_DelayMs(40);  BEEP_OFF;
#endif
}
