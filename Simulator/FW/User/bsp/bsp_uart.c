/**
  ******************************************************************************
  * @file    bsp_uart.c
  * @brief   USART1 中断收发
  ******************************************************************************
  */
#include "stm32f4xx.h"
#include <stdarg.h>
#include <stdio.h>
#include "bsp_uart.h"

#define UART_USART        USART1
#define UART_APB           RCC_APB2PeriphClockCmd
#define UART_APB_PERIPH    RCC_APB2Periph_USART1
#define UART_GPIO_PORT     GPIOA
#define UART_GPIO_CLK     RCC_AHB1Periph_GPIOA
#define UART_TX_PIN        GPIO_Pin_9
#define UART_RX_PIN        GPIO_Pin_10
#define UART_AF            GPIO_AF_USART1
#define UART_IRQ           USART1_IRQn
#define UART_ISR           USART1_IRQHandler

#define TX_RING_SIZE       1024u
#define RX_RING_SIZE       256u

typedef struct
{
    volatile uint16_t head;
    volatile uint16_t tail;
    uint8_t buf[TX_RING_SIZE];
} Ring8_t;

static Ring8_t tx_ring;
static Ring8_t rx_ring;

/* ---------- 环缓冲（单生产 / 单消费，无锁） ---------- */
static uint16_t ring_next(uint16_t i, uint16_t size)
{
    return (uint16_t)((i + 1u) >= size ? 0u : i + 1u);
}

void BSP_UART_Init(uint32_t baud)
{
    GPIO_InitTypeDef  GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;
    NVIC_InitTypeDef  NVIC_InitStructure;

    tx_ring.head = tx_ring.tail = 0;
    rx_ring.head = rx_ring.tail = 0;

    /* GPIO 时钟 + USART 时钟 */
    RCC_AHB1PeriphClockCmd(UART_GPIO_CLK, ENABLE);
    UART_APB(UART_APB_PERIPH, ENABLE);

    /* 复用映射 */
    GPIO_PinAFConfig(UART_GPIO_PORT, GPIO_PinSource9,  UART_AF);
    GPIO_PinAFConfig(UART_GPIO_PORT, GPIO_PinSource10, UART_AF);

    /* TX / RX 引脚：复用推挽 */
    GPIO_InitStructure.GPIO_Pin   = UART_TX_PIN | UART_RX_PIN;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(UART_GPIO_PORT, &GPIO_InitStructure);

    /* USART：8N1，无流控 */
    USART_InitStructure.USART_BaudRate   = baud;
    USART_InitStructure.USART_WordLength  = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits   = USART_StopBits_1;
    USART_InitStructure.USART_Parity      = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode       = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(UART_USART, &USART_InitStructure);

    /* RXNE 中断 */
    USART_ITConfig(UART_USART, USART_IT_RXNE, ENABLE);

    NVIC_InitStructure.NVIC_IRQChannel                   = UART_IRQ;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    USART_Cmd(UART_USART, ENABLE);
}

/* 启动发送（若空闲则触发首次 TXE） */
static void uart_kick_tx(void)
{
    USART_ITConfig(UART_USART, USART_IT_TXE, ENABLE);
}

void BSP_UART_Send(const uint8_t *data, uint16_t len)
{
    uint16_t i;
    if (len == 0) return;
    for (i = 0; i < len; i++)
    {
        uint16_t nh = ring_next(tx_ring.head, TX_RING_SIZE);
        while (nh == tx_ring.tail) { ; }   /* 环满：等中断清出空间（极短） */
        tx_ring.buf[tx_ring.head] = data[i];
        __DMB();
        tx_ring.head = nh;
    }
    uart_kick_tx();
}

void BSP_UART_PutByte(uint8_t b)
{
    BSP_UART_Send(&b, 1);
}

void BSP_UART_Printf(const char *fmt, ...)
{
    char s[160];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(s, sizeof(s), fmt, ap);
    va_end(ap);
    if (n > 0)
    {
        if (n > (int)sizeof(s)) n = (int)sizeof(s);
        BSP_UART_Send((const uint8_t *)s, (uint16_t)n);
    }
}

uint8_t BSP_UART_GetByte(uint8_t *out)
{
    if (rx_ring.head == rx_ring.tail) return 0;
    *out = rx_ring.buf[rx_ring.tail];
    __DMB();
    rx_ring.tail = ring_next(rx_ring.tail, RX_RING_SIZE);
    return 1;
}

uint16_t BSP_UART_RX_Avail(void)
{
    int16_t d = (int16_t)rx_ring.head - (int16_t)rx_ring.tail;
    return (uint16_t)((d < 0) ? d + (int16_t)RX_RING_SIZE : d);
}

uint16_t BSP_UART_TX_Avail(void)
{
    int16_t d = (int16_t)tx_ring.head - (int16_t)tx_ring.tail;
    return (uint16_t)((d < 0) ? d + (int16_t)TX_RING_SIZE : d);
}

void BSP_UART_TX_Flush(void)
{
    while (BSP_UART_TX_Avail() > 0) { ; }
}

/* --------------------- 中断服务 --------------------- */
void UART_ISR(void)
{
    /* ---- 接收 ---- */
    if (USART_GetITStatus(UART_USART, USART_IT_RXNE) != RESET)
    {
        uint8_t b = (uint8_t)USART_ReceiveData(UART_USART);
        uint16_t nh = ring_next(rx_ring.head, RX_RING_SIZE);
        if (nh != rx_ring.tail)               /* 未满则入队，满则丢弃最旧 */
        {
            rx_ring.buf[rx_ring.head] = b;
            __DMB();
            rx_ring.head = nh;
        }
        /* 读 DR 清标志已由 ReceiveData 完成 */
    }

    /* ---- 发送 ---- */
    if (USART_GetITStatus(UART_USART, USART_IT_TXE) != RESET)
    {
        if (tx_ring.head != tx_ring.tail)
        {
            USART_SendData(UART_USART, tx_ring.buf[tx_ring.tail]);
            __DMB();
            tx_ring.tail = ring_next(tx_ring.tail, TX_RING_SIZE);
        }
        else
        {
            /* 环空，关 TXE，保留 TC 以让最后一字节真正送出 */
            USART_ITConfig(UART_USART, USART_IT_TXE, DISABLE);
        }
    }

    /* 清溢出标志，避免反复进中断 */
    if (USART_GetFlagStatus(UART_USART, USART_FLAG_ORE) != RESET)
    {
        (void)UART_USART->DR;
        (void)UART_USART->SR;
    }
}
