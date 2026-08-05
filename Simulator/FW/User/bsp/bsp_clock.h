/**
  ******************************************************************************
  * @file    bsp_clock.h
  * @brief   时钟 + SysTick 毫秒节拍
  *          统一使用 HSI->PLL 168MHz，与板上晶振是否焊接无关，UART/SPI 波特率确定。
  ******************************************************************************
  */
#ifndef __BSP_CLOCK_H
#define __BSP_CLOCK_H

#include <stdint.h>

#define SYSCLK_HZ        168000000u
#define HCLK_HZ          168000000u
#define PCLK1_HZ          42000000u   /* APB1 */
#define PCLK2_HZ          84000000u   /* APB2 */

void    BSP_Clock_Init(void);     /* HSI->PLL 168MHz                  */
void    BSP_SysTick_Init(void);  /* 1ms 节拍                          */
uint32_t BSP_Millis(void);       /* 自上电毫秒数                      */
uint32_t BSP_Micros(void);       /* 自上电微秒数（SysTick 推算）       */
void    BSP_DelayMs(uint32_t ms);

#endif /* __BSP_CLOCK_H */
