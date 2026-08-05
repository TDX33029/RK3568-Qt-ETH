/**
  ******************************************************************************
  * @file    sim.h
  * @brief   仿真发生器：目标轨迹 -> 每锚点 TOA/TDOA/AOA/RSS -> 182B SpiFrame
  *          与 RK3568 端 tracker3d_simulate_measurement 模型一致。
  *          双缓冲：SIM_Step 写 shadow，SPI DMA 读 live，原子交换。
  ******************************************************************************
  */
#ifndef __SIM_H
#define __SIM_H

#include <stdint.h>
#include "proto.h"

void SIM_Init(void);
void SIM_ApplyConfig(const ProtoConfig_t *cfg);   /* 切场景/参数：重置目标状态 */
void SIM_Reset(void);                              /* 复位 seq + 目标到场景初值 */

/* 推进一帧：更新目标位置、填充 SpiFrame、交换缓冲、seq++。
 * now_us = 当前微秒时间戳 (BSP_Micros)，用于 dt_us */
void SIM_Step(uint32_t now_us);

SpiFrame_t *SIM_GetLiveFrame(void);    /* SPI DMA 当前读取的帧 */
void        SIM_GetTruth(ProtoTruth_t *out);

#endif /* __SIM_H */
