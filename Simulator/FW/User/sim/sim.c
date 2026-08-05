/**
  ******************************************************************************
  * @file    sim.c
  * @brief   目标轨迹 + 每锚点 TOA/TDOA/AOA/RSS 仿真，填充 182B SpiFrame（双缓冲）
  ******************************************************************************
  */
#include "stm32f4xx.h"
#include <math.h>
#include "sim.h"
#include "bsp_clock.h"
#include "proto.h"

/* ---- 物理常量 (与 RK3568 tracker3d_default_config 一致) ---- */
#define C_LIGHT        299792458.0f
#define TDOA_STD_SEC   2.0e-9f
#define TOA_STD_SEC    2.0e-9f
#define AOA_STD_RAD    (1.0f * 0.017453293f)   /* 1 度 */
#define RSS_STD_DB     2.0f
#define RSS_REF_DBM   -35.0f
#define RSS_PATH_N     2.0f
#define EPS            1.0e-3f

#define N_ANC          5u
#define REF_ANC        0u

/* 锚点坐标 (与 RK configure_demo_anchors 相同) */
static const float ANC_X[N_ANC] = { 0.0f, 30.0f, 30.0f, 0.0f, 15.0f };
static const float ANC_Y[N_ANC] = { 0.0f,  0.0f, 30.0f, 30.0f, 15.0f };
static const float ANC_Z[N_ANC] = { 0.0f,  4.0f,  0.0f,  5.0f, 12.0f };

/* 运动边界 (目标保持在锚点区域内) */
#define BOUND_XY       40.0f
#define BOUND_Z_LO      0.5f
#define BOUND_Z_HI     20.0f

/* ---- 目标状态 [x,y,z, vx,vy,vz] ---- */
typedef struct { float x, y, z, vx, vy, vz; } Target_t;
static Target_t g_t;
static uint8_t  g_scene;
static uint8_t  g_enable;
static uint32_t g_rate;

/* ---- 双缓冲 ---- */
static SpiFrame_t g_buf[2];
static volatile uint8_t g_write = 1;   /* 生成器写 */
static volatile uint8_t g_live  = 0;   /* SPI DMA 读 */
static uint16_t g_seq = 0;
static uint32_t g_last_us = 0;
static uint8_t  g_frame_count = 0;     /* 仅用于状态上报的低字节 */

/* ---- PRNG + 高斯 (xorshift32 + Box-Muller) ---- */
static uint32_t prng_state = 0xA5C0FFEEu;
static uint32_t xorshift32(void)
{
    uint32_t x = prng_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    prng_state = x; return x;
}
static float frand(void)
{
    return (float)(xorshift32() >> 8) / (float)0x00FFFFFFu;
}
static float gauss(void)
{
    float u1 = frand(), u2 = frand();
    if (u1 < 1e-7f) u1 = 1e-7f;
    return sqrtf(-2.0f * logf(u1)) * cosf(2.0f * 3.14159265358979f * u2);
}

/* ---- 场景初值 (与 RK set_base_truth_state 一致) ---- */
static void scene_init(uint8_t scene, Target_t *t)
{
    switch (scene)
    {
        case SIM_SCENE_CLIMB:
            t->x=6.0f;  t->y=8.0f;  t->z=4.0f;
            t->vx=1.05f; t->vy=0.65f; t->vz=0.35f; break;
        case SIM_SCENE_TURN:
            t->x=10.0f; t->y=7.0f;  t->z=5.5f;
            t->vx=1.10f; t->vy=0.25f; t->vz=0.10f; break;
        case SIM_SCENE_STRAIGHT:
        default:
            t->x=8.0f;  t->y=10.0f; t->z=6.0f;
            t->vx=1.20f; t->vy=0.75f; t->vz=0.20f; break;
    }
}

void SIM_ApplyConfig(const ProtoConfig_t *cfg)
{
    g_scene  = cfg->scene;
    g_enable = cfg->enable_mask;
    g_rate   = cfg->sample_rate_hz;
    if (g_rate == 0u) g_rate = 100u;

    if (cfg->scene == SIM_SCENE_CUSTOM)
    {
        g_t.x = cfg->init_x; g_t.y = cfg->init_y; g_t.z = cfg->init_z;
        g_t.vx = cfg->vel_x; g_t.vy = cfg->vel_y; g_t.vz = cfg->vel_z;
    }
    else
    {
        scene_init(cfg->scene, &g_t);
    }
    g_last_us = BSP_Micros();
}

void SIM_Reset(void)
{
    g_seq = 0;
    if (g_scene == SIM_SCENE_CUSTOM)
    {
        /* 保持配置的初值不变（由 ApplyConfig 已设置） */
    }
    else
    {
        scene_init(g_scene, &g_t);
    }
    g_last_us = BSP_Micros();
}

void SIM_Init(void)
{
    SpiFrame_t *f = &g_buf[0];
    /* 产出一个合法空帧，保证 SPI 首次读取魔数/CRC 正确 */
    uint8_t i;
    for (i = 0; i < (uint8_t)sizeof(SpiFrame_t); i++) ((uint8_t *)f)[i] = 0;
    f->magic[0] = SPI_FRAME_MAGIC0;
    f->magic[1] = SPI_FRAME_MAGIC1;
    f->seq = 0;
    f->mode_mask = 0;
    f->n_anc = N_ANC;
    f->dt_us = 0;
    f->reserved = 0;
    SpiFrame_Finalize(f);
    g_live = 0; g_write = 1; g_seq = 0;
}

SpiFrame_t *SIM_GetLiveFrame(void) { return (SpiFrame_t *)&g_buf[g_live]; }

void SIM_GetTruth(ProtoTruth_t *out)
{
    out->x = g_t.x; out->y = g_t.y; out->z = g_t.z;
    out->vx = g_t.vx; out->vy = g_t.vy; out->vz = g_t.vz;
}

/* 推进目标位置（恒速度 + 边界反弹） */
static void advance_target(float dt)
{
    g_t.x += g_t.vx * dt;
    g_t.y += g_t.vy * dt;
    g_t.z += g_t.vz * dt;
    if (g_t.x >  BOUND_XY) { g_t.x =  BOUND_XY; g_t.vx = -g_t.vx; }
    if (g_t.x < -BOUND_XY) { g_t.x = -BOUND_XY; g_t.vx = -g_t.vx; }
    if (g_t.y >  BOUND_XY) { g_t.y =  BOUND_XY; g_t.vy = -g_t.vy; }
    if (g_t.y < -BOUND_XY) { g_t.y = -BOUND_XY; g_t.vy = -g_t.vy; }
    if (g_t.z > BOUND_Z_HI) { g_t.z = BOUND_Z_HI; g_t.vz = -g_t.vz; }
    if (g_t.z < BOUND_Z_LO) { g_t.z = BOUND_Z_LO; g_t.vz = -g_t.vz; }
}

void SIM_Step(uint32_t now_us)
{
    SpiFrame_t *f;
    float dt, dx, dy, dz, rho, range, rrange;
    uint8_t i;
    uint8_t en = g_enable;
    uint32_t dt_us;

    /* dt（秒） */
    dt_us = now_us - g_last_us;
    g_last_us = now_us;
    if (dt_us > 1000000u) dt_us = 1000000u;   /* 防溢出巨跳 */
    dt = (float)dt_us * 1.0e-6f;

    advance_target(dt);

    /* 写到 shadow 缓冲 */
    f = &g_buf[g_write];
    for (i = 0; i < (uint8_t)sizeof(SpiFrame_t); i++) ((uint8_t *)f)[i] = 0;

    f->magic[0]  = SPI_FRAME_MAGIC0;
    f->magic[1]  = SPI_FRAME_MAGIC1;
    f->seq       = g_seq;
    f->mode_mask = en;
    f->n_anc     = N_ANC;
    f->dt_us     = dt_us;
    f->reserved  = 0;
    g_seq++;

    /* 参考锚距离 (A0) */
    dx = g_t.x - ANC_X[REF_ANC]; dy = g_t.y - ANC_Y[REF_ANC]; dz = g_t.z - ANC_Z[REF_ANC];
    rho = sqrtf(dx*dx + dy*dy); if (rho < EPS) rho = EPS;
    rrange = sqrtf(rho*rho + dz*dz); if (rrange < EPS) rrange = EPS;

    for (i = 0; i < N_ANC; i++)
    {
        uint8_t has = 0;
        dx = g_t.x - ANC_X[i]; dy = g_t.y - ANC_Y[i]; dz = g_t.z - ANC_Z[i];
        rho = sqrtf(dx*dx + dy*dy); if (rho < EPS) rho = EPS;
        range = sqrtf(rho*rho + dz*dz); if (range < EPS) range = EPS;

        if (en & SPI_MODE_TDOA)
        {
            if (i != REF_ANC)
            {
                has |= SPI_MODE_TDOA;
                f->anchors[i].tdoa_sec = (range - rrange) / C_LIGHT + gauss() * TDOA_STD_SEC;
            }
        }
        if (en & SPI_MODE_TOA)
        {
            has |= SPI_MODE_TOA;
            f->anchors[i].toa_sec = range / C_LIGHT + gauss() * TOA_STD_SEC;
        }
        if (en & SPI_MODE_AOA)
        {
            has |= SPI_MODE_AOA;
            f->anchors[i].aoa_az = atan2f(dy, dx) + gauss() * AOA_STD_RAD;
            f->anchors[i].aoa_el = atan2f(dz, rho) + gauss() * AOA_STD_RAD;
        }
        if (en & SPI_MODE_RSS)
        {
            has |= SPI_MODE_RSS;
            f->anchors[i].rss_dbm = RSS_REF_DBM - 10.0f * RSS_PATH_N * log10f(range) + gauss() * RSS_STD_DB;
        }
        f->anchors[i].has = has;
    }
    /* anchors[5..7] 已清零 (has=0) */

    SpiFrame_Finalize(f);

    /* 原子交换：live <- write, write <- 1-write */
    __disable_irq();
    g_live = g_write;
    g_write = 1u - g_write;
    __enable_irq();

    g_frame_count++;
}
