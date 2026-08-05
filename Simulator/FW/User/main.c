/**
  ******************************************************************************
  * @file    main.c
  * @brief   上位机虚拟传感器固件入口
  *          F407ZGT6 经 SPI 连续发出仿真 TOA/AOA/TDOA，经 USART1 受上位机控制/回传。
  ******************************************************************************
  */
#include "stm32f4xx.h"
#include "bsp_clock.h"
#include "bsp_uart.h"
#include "bsp_spi.h"
#include "bsp_timer.h"
#include "app.h"

int main(void)
{
    /* 1. 时钟（HSI->PLL 168MHz，与板上晶振无关） + SysTick 1ms */
    BSP_Clock_Init();
    BSP_SysTick_Init();

    /* 2. 外设 */
    BSP_UART_Init(BSP_UART_BAUD_DEFAULT);   /* PC 控制链路 */
    BSP_SPI_Init();                         /* SPI 数据链路 */
    BSP_Timer_Init();                       /* 采样率节拍 */

    /* 3. 应用层（含默认配置、启动短鸣） */
    APP_Init();

    /* 4. 主循环 */
    while (1)
    {
        APP_Step();
    }
}

/*********************************************END OF FILE**********************/
