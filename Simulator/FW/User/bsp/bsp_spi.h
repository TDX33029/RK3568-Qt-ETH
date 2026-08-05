/**
  ******************************************************************************
  * @file    bsp_spi.h
  * @brief   SPI2 从机：RK3568(主机) 轮询读取 182B SpiFrame
  *          PB13 SCK / PB14 MISO / PB15 MOSI / PB12 NSS (硬件)
  *          TX-DMA(DMA1_Stream4/Ch0) 从 live 帧送出，TC 中断重装。
  ******************************************************************************
  */
#ifndef __BSP_SPI_H
#define __BSP_SPI_H

#include <stdint.h>

void     BSP_SPI_Init(void);     /* 配置 SPI2 从机 + DMA（不启动 DMA） */
void     BSP_SPI_Arm(void);      /* 装载 live 帧并启动 DMA（SIM_Init 之后调用） */
uint32_t BSP_SPI_GetXferCount(void);

#endif /* __BSP_SPI_H */
