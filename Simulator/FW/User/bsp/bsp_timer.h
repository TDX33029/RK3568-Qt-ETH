/**
  ******************************************************************************
  * @file    bsp_timer.h
  * @brief   TIM2（32bit，APB1=84MHz）采样率节拍
  *          中断按 sample_rate 触发，置位帧请求计数；主循环消费。
  ******************************************************************************
  */
#ifndef __BSP_TIMER_H
#define __BSP_TIMER_H

#include <stdint.h>

void     BSP_Timer_Init(void);
void     BSP_Timer_SetRate(uint32_t hz);   /* 1..5000 Hz */
void     BSP_Timer_Start(void);
void     BSP_Timer_Stop(void);

uint16_t BSP_Timer_TakeRequests(void);     /* 取走待生成的帧数（原子读并清零） */
void     BSP_Timer_AddRequest(void);       /* 软件触发一次（调试用） */

uint32_t BSP_Timer_GetDropped(void);       /* 因主循环跟不上而被丢弃的请求数 */
uint8_t  BSP_Timer_IsRunning(void);

#endif /* __BSP_TIMER_H */
