/**
  ******************************************************************************
  * @file    bsp_uart.h
  * @brief   USART1 (PA9/PA10) —— PC 上位机控制 + 遥测链路
  *          中断收发，环形缓冲；非阻塞。
  ******************************************************************************
  */
#ifndef __BSP_UART_H
#define __BSP_UART_H

#include <stdint.h>

#define BSP_UART_BAUD_DEFAULT   115200u

void     BSP_UART_Init(uint32_t baud);

/* 阻塞写一个字节（仅启动时用） */
void     BSP_UART_PutByte(uint8_t b);

/* 非阻塞批量发送：拷入发送环缓冲，剩余部分由 TXE 中断送出 */
void     BSP_UART_Send(const uint8_t *data, uint16_t len);

/* 类 printf 调试输出（非阻塞，进环缓冲） */
void     BSP_UART_Printf(const char *fmt, ...);

/* 读一个收到的字节，返回 1 表示有数据 */
uint8_t  BSP_UART_GetByte(uint8_t *out);

/* 收 / 发 环缓冲中待处理字节数 */
uint16_t BSP_UART_RX_Avail(void);
uint16_t BSP_UART_TX_Avail(void);

void     BSP_UART_TX_Flush(void);   /* 阻塞等待发送环缓冲清空 */

#endif /* __BSP_UART_H */
