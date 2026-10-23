#include "console.h"
#include "board.h"
#include "stm32f10x.h"

#define LINE_SIZE 64U

static volatile char    s_line[LINE_SIZE];// 存整条输入的指令
static volatile uint8_t s_len;// 当前存了几个字符
static volatile bool    s_ready;// 标记：是不是收到完整一行（收到回车=1）

/* 阻塞发送一个字节 */
static void uart_tx(uint8_t b)
{
    while ((USART1->SR & USART_SR_TXE) == 0U) {
    }
    USART1->DR = b;
}

/* 串口接收中断：直接在中断里拼一行 */
void USART1_IRQHandler(void)
{
    uint32_t sr = USART1->SR;

    if ((sr & USART_SR_RXNE) != 0U) {
        uint8_t c = (uint8_t)USART1->DR;

        if (c == '\r' || c == '\n') {
            if ((s_len > 0U) && !s_ready) {
                s_line[s_len] = '\0';
                s_ready = true;
            }
        } else if ((c == 0x08U) || (c == 0x7FU)) {//退格 删除键
            if ((s_len > 0U) && !s_ready) {
                s_len--;
            }
        } else if ((c >= 0x20U) && (c <= 0x7EU)) {
            if ((s_len < (LINE_SIZE - 1U)) && !s_ready) {
                s_line[s_len] = (char)c;
                s_len++;
            }
        }
    } else if ((sr & (USART_SR_ORE | USART_SR_FE |
                      USART_SR_NE | USART_SR_PE)) != 0U) {
        (void)USART1->DR;   /* 清错误标志 */
    }
}

void console_init(void)
{
    GPIO_InitTypeDef  gpio;
    USART_InitTypeDef usart;
    NVIC_InitTypeDef  nvic;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1 |RCC_APB2Periph_GPIOA, ENABLE);

    gpio.GPIO_Pin   = GPIO_Pin_9;
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);

    gpio.GPIO_Pin   = GPIO_Pin_10;
    gpio.GPIO_Mode  = GPIO_Mode_IPU;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);

    usart.USART_BaudRate            = 9600;
    usart.USART_WordLength          = USART_WordLength_8b;
    usart.USART_StopBits            = USART_StopBits_1;
    usart.USART_Parity              = USART_Parity_No;
    usart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    usart.USART_Mode                = USART_Mode_Tx | USART_Mode_Rx;
    USART_Init(USART1, &usart);

    USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);

    nvic.NVIC_IRQChannel                   = USART1_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 1;
    nvic.NVIC_IRQChannelSubPriority        = 0;
    nvic.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&nvic);

    USART_Cmd(USART1, ENABLE);
}

/* 打印字符串 */
void console_print(const char *s)
{
    if (s == 0) {
        return;
    }
    while (*s != '\0') {
        uart_tx((uint8_t)*s);
        s++;
    }
}

/* 打印无符号整数 */
void console_print_u32(uint32_t v)
{
    char    buf[10];
    uint8_t n = 0U;

    if (v == 0U) {
        uart_tx('0');
        return;
    }

    while ((v > 0U) && (n < sizeof(buf))) {
        buf[n++] = (char)('0' + (v % 10U));
        v /= 10U;
    }

    while (n > 0U) {
        uart_tx((uint8_t)buf[--n]);
    }
}

/* 主循环取一行 */
bool console_read_line(char *line, uint16_t max_len)
{
    uint16_t i;

    if ((line == 0) || (max_len < 2U) || !s_ready) {
        return false;
    }

    for (i = 0U; (i < (uint16_t)(max_len - 1U)) &&
                (s_line[i] != '\0'); i++) {
        line[i] = s_line[i];
    }
    line[i] = '\0';

    s_len = 0U;
    s_ready = false;

    return true;
}
