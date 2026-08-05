/**
  ******************************************************************************
  * @file    bsp_timer.c
  * @brief   TIM2 32bit 采样节拍
  ******************************************************************************
  */
#include "stm32f4xx.h"
#include "bsp_timer.h"

#define TIMx               TIM2
#define TIM_RCC            RCC_APB1PeriphClockCmd
#define TIM_RCC_PERIPH    RCC_APB1Periph_TIM2
#define TIM_IRQ            TIM2_IRQn
#define TIM_ISR            TIM2_IRQHandler

#define TIM_CLK_HZ         84000000u     /* APB1 timer clock = 2 * 42MHz */
#define PENDING_CAP        32u           /* 主循环跟不上时丢弃的阈值 */

static volatile uint16_t g_pending = 0;
static volatile uint32_t g_dropped = 0;
static volatile uint8_t  g_running = 0;
static uint32_t          g_cur_rate_hz = 0;

void BSP_Timer_Init(void)
{
    TIM_RCC(TIM_RCC_PERIPH, ENABLE);

    NVIC_SetPriority(TIM_IRQ, 2);
    NVIC_EnableIRQ(TIM_IRQ);
}

void BSP_Timer_SetRate(uint32_t hz)
{
    TIM_TimeBaseInitTypeDef tb;
    if (hz < 1u)   hz = 1u;
    if (hz > 5000u) hz = 5000u;
    g_cur_rate_hz = hz;

    TIM_DeInit(TIMx);
    /* ARR = TIM_CLK_HZ / hz - 1 ; 32bit 定时器足够覆盖 1..5000Hz */
    tb.TIM_Prescaler         = 0;
    tb.TIM_CounterMode       = TIM_CounterMode_Up;
    tb.TIM_Period            = (TIM_CLK_HZ / hz) - 1u;
    tb.TIM_ClockDivision     = TIM_CKD_DIV1;
    tb.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIMx, &tb);

    TIM_ClearFlag(TIMx, TIM_IT_Update);
    TIM_ITConfig(TIMx, TIM_IT_Update, ENABLE);
}

void BSP_Timer_Start(void)
{
    g_pending = 0;
    g_running = 1;
    TIM_SetCounter(TIMx, 0);
    TIM_Cmd(TIMx, ENABLE);
}

void BSP_Timer_Stop(void)
{
    TIM_Cmd(TIMx, DISABLE);
    g_running = 0;
    g_pending = 0;
}

uint16_t BSP_Timer_TakeRequests(void)
{
    /* 进入临界区读取并清零，避免与 ISR 竞争 */
    uint16_t v;
    __disable_irq();
    v = g_pending;
    g_pending = 0;
    __enable_irq();
    return v;
}

void BSP_Timer_AddRequest(void)
{
    __disable_irq();
    if (g_pending < PENDING_CAP) g_pending++;
    else                         g_dropped++;
    __enable_irq();
}

uint32_t BSP_Timer_GetDropped(void) { return g_dropped; }
uint8_t  BSP_Timer_IsRunning(void)  { return g_running; }

void TIM_ISR(void)
{
    if (TIM_GetITStatus(TIMx, TIM_IT_Update) != RESET)
    {
        TIM_ClearITPendingBit(TIMx, TIM_IT_Update);
        if (g_running)
        {
            if (g_pending < PENDING_CAP) g_pending++;
            else                         g_dropped++;
        }
    }
}
