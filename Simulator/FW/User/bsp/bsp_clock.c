/**
  ******************************************************************************
  * @file    bsp_clock.c
  * @brief   HSI->PLL 168MHz + SysTick 1ms
  ******************************************************************************
  */
#include "stm32f4xx.h"
#include "bsp_clock.h"

/* SystemCoreClock 定义于 system_stm32f4xx.c，重配后同步它 */
extern uint32_t SystemCoreClock;

volatile uint32_t g_ms_ticks = 0;

/* HSI 16MHz -> 168MHz:  VCO = (16/16)*336 = 336MHz, /2 = 168MHz */
#define PLL_M   16
#define PLL_N   336
#define PLL_P     2
#define PLL_Q     7

void BSP_Clock_Init(void)
{
    RCC_DeInit();

    /* 打开 HSI 并等待就绪 */
    RCC_HSICmd(ENABLE);
    while (RCC_GetFlagStatus(RCC_FLAG_HSIRDY) == RESET) { ; }

    /* Flash 等待周期：168MHz / 3.3V / Scale1 需要 5 WS */
    FLASH_SetLatency(FLASH_Latency_5);
    FLASH_PrefetchBufferCmd(ENABLE);
    FLASH_InstructionCacheCmd(ENABLE);
    FLASH_DataCacheCmd(ENABLE);

    /* 总线分频：AHB=168, APB1=42, APB2=84 */
    RCC_HCLKConfig(RCC_SYSCLK_Div1);
    RCC_PCLK1Config(RCC_HCLK_Div4);
    RCC_PCLK2Config(RCC_HCLK_Div2);

    /* PLL：HIS / 16 * 336 / 2 = 168MHz */
    RCC_PLLConfig(RCC_PLLSource_HSI, PLL_M, PLL_N, PLL_P, PLL_Q);
    RCC_PLLCmd(ENABLE);
    while (RCC_GetFlagStatus(RCC_FLAG_PLLRDY) == RESET) { ; }

    /* 切到 PLL 作为系统时钟 */
    RCC_SYSCLKConfig(RCC_SYSCLKSource_PLLCLK);
    while (RCC_GetSYSCLKSource() != 0x08) { ; }   /* 等待 PLL 作为 SYSCLK */

    SystemCoreClock = SYSCLK_HZ;
}

void BSP_SysTick_Init(void)
{
    /* SysTick 时钟源 = HCLK = 168MHz，重装 168000-1 => 1ms */
    SysTick_Config(SystemCoreClock / 1000u);
}

uint32_t BSP_Millis(void)
{
    return g_ms_ticks;
}

uint32_t BSP_Micros(void)
{
    uint32_t ms;
    uint32_t ticks;
    do
    {
        ms    = g_ms_ticks;
        ticks = SysTick->VAL;
    } while (ms != g_ms_ticks);   /* 防止 ms 跨界 */

    /* SysTick 是向下计数：剩余 ticks 对应 (ticks)/168 us */
    return ms * 1000u + ((SystemCoreClock / 1000u - 1u) - ticks) / (SystemCoreClock / 1000000u);
}

void BSP_DelayMs(uint32_t ms)
{
    uint32_t start = BSP_Millis();
    while ((BSP_Millis() - start) < ms) { ; }
}

/* SysTick 中断：1ms 节拍 */
void SysTick_Handler(void)
{
    g_ms_ticks++;
}
