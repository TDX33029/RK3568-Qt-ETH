/**
  ******************************************************************************
  * @file    bsp_spi.c
  * @brief   SPI2 从机 + TX-DMA，响应 RK3568 主机轮询，返回 182B SpiFrame
  ******************************************************************************
  */
#include "stm32f4xx.h"
#include "bsp_spi.h"
#include "bsp_clock.h"
#include "sim.h"
#include "proto.h"

#define SPI_PORT          SPI2
#define SPI_RCC            RCC_APB1PeriphClockCmd
#define SPI_RCC_PERIPH    RCC_APB1Periph_SPI2

#define SPI_GPIO_PORT     GPIOB
#define SPI_GPIO_CLK     RCC_AHB1Periph_GPIOB
#define SPI_SCK_PIN       GPIO_Pin_13
#define SPI_MOSI_PIN      GPIO_Pin_15
#define SPI_NSS_PIN       GPIO_Pin_12
/* MISO 走 PC2 (AF5, SPI2_MISO) */
#define SPI_MISO_PORT     GPIOC
#define SPI_MISO_CLK      RCC_AHB1Periph_GPIOC
#define SPI_MISO_PIN      GPIO_Pin_2
#define SPI_AF            GPIO_AF_SPI2

/* SPI2_TX = DMA1 Stream4 Channel0 */
#define SPI_DMAx          DMA1
#define SPI_DMA_STREAM    DMA1_Stream4
#define SPI_DMA_CHANNEL   DMA_Channel_0
#define SPI_DMA_CLK       RCC_AHB1Periph_DMA1
#define SPI_DMA_IRQn       DMA1_Stream4_IRQn
#define SPI_DMA_ISR       DMA1_Stream4_IRQHandler

static volatile uint32_t g_xfer_count = 0;

void BSP_SPI_Init(void)
{
    GPIO_InitTypeDef  GPIO_InitStructure;
    SPI_InitTypeDef   SPI_InitStructure;
    DMA_InitTypeDef   DMA_InitStructure;
    NVIC_InitTypeDef  NVIC_InitStructure;

    RCC_AHB1PeriphClockCmd(SPI_GPIO_CLK, ENABLE);
    RCC_AHB1PeriphClockCmd(SPI_MISO_CLK, ENABLE);   /* GPIOC for MISO */
    SPI_RCC(SPI_RCC_PERIPH, ENABLE);
    RCC_AHB1PeriphClockCmd(SPI_DMA_CLK, ENABLE);

    /* SCK(PB13) / MOSI(PB15) / NSS(PB12) 复用 AF5；MISO(PC2) 在 GPIOC */
    GPIO_PinAFConfig(SPI_GPIO_PORT, GPIO_PinSource12, SPI_AF);  /* NSS  PB12 */
    GPIO_PinAFConfig(SPI_GPIO_PORT, GPIO_PinSource13, SPI_AF); /* SCK   PB13 */
    GPIO_PinAFConfig(SPI_GPIO_PORT, GPIO_PinSource15, SPI_AF); /* MOSI  PB15 */
    GPIO_PinAFConfig(SPI_MISO_PORT, GPIO_PinSource2, SPI_AF);  /* MISO  PC2  */

    GPIO_InitStructure.GPIO_Pin   = SPI_NSS_PIN | SPI_SCK_PIN | SPI_MOSI_PIN;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_UP;     /* 空闲拉高 */
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(SPI_GPIO_PORT, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin = SPI_MISO_PIN;       /* PC2 MISO */
    GPIO_Init(SPI_MISO_PORT, &GPIO_InitStructure);

    /* SPI2 从机：模式0，8bit，MSB，硬件 NSS，全双工（MOSI 忽略） */
    SPI_InitStructure.SPI_Direction        = SPI_Direction_2Lines_FullDuplex;
    SPI_InitStructure.SPI_Mode              = SPI_Mode_Slave;
    SPI_InitStructure.SPI_DataSize         = SPI_DataSize_8b;
    SPI_InitStructure.SPI_CPOL            = SPI_CPOL_Low;
    SPI_InitStructure.SPI_CPHA            = SPI_CPHA_1Edge;
    SPI_InitStructure.SPI_NSS             = SPI_NSS_Hard;
    SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_2;  /* 从机不用，填合法值 */
    SPI_InitStructure.SPI_FirstBit        = SPI_FirstBit_MSB;
    SPI_InitStructure.SPI_CRCPolynomial   = 7;
    SPI_Init(SPI_PORT, &SPI_InitStructure);

    /* TX DMA: memory -> SPI2_DR, 普通 182B, 字节, TC 中断 */
    DMA_DeInit(SPI_DMA_STREAM);
    DMA_InitStructure.DMA_Channel            = SPI_DMA_CHANNEL;
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&SPI_PORT->DR;
    DMA_InitStructure.DMA_Memory0BaseAddr    = (uint32_t)0;       /* Arm 时再设 */
    DMA_InitStructure.DMA_DIR                = DMA_DIR_MemoryToPeripheral;
    DMA_InitStructure.DMA_BufferSize         = SPI_FRAME_LEN;
    DMA_InitStructure.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;
    DMA_InitStructure.DMA_MemoryInc          = DMA_MemoryInc_Enable;
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    DMA_InitStructure.DMA_MemoryDataSize     = DMA_MemoryDataSize_Byte;
    DMA_InitStructure.DMA_Mode               = DMA_Mode_Normal;
    DMA_InitStructure.DMA_Priority           = DMA_Priority_High;
    DMA_InitStructure.DMA_FIFOMode          = DMA_FIFOMode_Disable;
    DMA_InitStructure.DMA_FIFOThreshold      = DMA_FIFOThreshold_HalfFull;
    DMA_InitStructure.DMA_MemoryBurst        = DMA_MemoryBurst_Single;
    DMA_InitStructure.DMA_PeripheralBurst    = DMA_PeripheralBurst_Single;
    DMA_Init(SPI_DMA_STREAM, &DMA_InitStructure);

    DMA_ITConfig(SPI_DMA_STREAM, DMA_IT_TC, ENABLE);

    NVIC_InitStructure.NVIC_IRQChannel                   = SPI_DMA_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    /* 清收发标志 */
    (void)SPI_I2S_ReceiveData(SPI_PORT);

    SPI_I2S_DMACmd(SPI_PORT, SPI_I2S_DMAReq_Tx, ENABLE);
    SPI_Cmd(SPI_PORT, ENABLE);
}

void BSP_SPI_Arm(void)
{
    DMA_Cmd(SPI_DMA_STREAM, DISABLE);
    while (DMA_GetCmdStatus(SPI_DMA_STREAM) != DISABLE) { ; }
    SPI_DMA_STREAM->M0AR = (uint32_t)SIM_GetLiveFrame();
    DMA_SetCurrDataCounter(SPI_DMA_STREAM, SPI_FRAME_LEN);
    DMA_Cmd(SPI_DMA_STREAM, ENABLE);
}

uint32_t BSP_SPI_GetXferCount(void) { return g_xfer_count; }

/* DMA 传输完成：RK 读完 182B，重装最新 live 帧供下次读取 */
void SPI_DMA_ISR(void)
{
    if (DMA_GetITStatus(SPI_DMA_STREAM, DMA_IT_TCIF4) != RESET)
    {
        DMA_ClearITPendingBit(SPI_DMA_STREAM, DMA_IT_TCIF4);
        g_xfer_count++;

        DMA_Cmd(SPI_DMA_STREAM, DISABLE);
        while (DMA_GetCmdStatus(SPI_DMA_STREAM) != DISABLE) { ; }
        SPI_DMA_STREAM->M0AR = (uint32_t)SIM_GetLiveFrame();
        DMA_SetCurrDataCounter(SPI_DMA_STREAM, SPI_FRAME_LEN);
        DMA_Cmd(SPI_DMA_STREAM, ENABLE);

        /* 清 OVR（读 SR 再读 DR），避免 RX 溢出影响 */
        (void)SPI_PORT->SR;
        (void)SPI_PORT->DR;
    }
}
