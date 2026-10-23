/*
 * ============================================================
 * ws2812.c —— WS2812B 驱动（TIM2_CH1 PWM + DMA1_Channel5）
 * ============================================================
 * WS2812B 的时序：
 *   一个 bit = 1.25us（800kHz）
 *   '0' 高电平约 0.4us（32%）
 *   '1' 高电平约 0.8us（64%）
 *   一帧结束后保持低电平 >50us，灯带才会锁存。
 *
 * 实现方式：
 *   - TIM2_CH1（PA0）输出 800kHz PWM；
 *   - DMA1_Channel5 每个 bit 把一个 CCR 值搬到 TIM2->CCR1；
 *   - CCR=32% 表示 0，CCR=64% 表示 1；
 *   - 一帧 = 12*24 个数据 bit + 64 个低电平复位 slot；
 *   - ws2812_show() 启动 DMA，轮询 TC 标志等发送完成。
 *
 * 注意：DMA1_Channel5 在 F103 上也映射到 USART1_RX；本工程串口用中断，
 * 不开 USART1 的 DMA 请求，所以不冲突。以后给串口加 DMA 要换通道。
 */

#include "ws2812.h"

#include "board.h"
#include "stm32f10x.h"

#define WS2812_BITS_PER_LED   24U   /* 每颗 24 bit：G/R/B 各 8 bit */
#define WS2812_RESET_SLOTS    64U   /* 帧尾低电平，约 80us，用于锁存 */
#define WS2812_FRAME_WORDS    (WS2812_LED_COUNT * WS2812_BITS_PER_LED + WS2812_RESET_SLOTS)

/* DMA 搬运的 CCR 值数组：每个元素对应一个 bit 的高电平时间 */
static uint16_t s_frame[WS2812_FRAME_WORDS];

/* 像素颜色缓存：s_pixels[i] = {R, G, B}，0~255 */
static uint8_t  s_pixels[WS2812_LED_COUNT][3];

/* 预先算好的 0/1 对应 CCR 值 */
static uint16_t s_ccr_zero;
static uint16_t s_ccr_one;

/*
 * 计算 TIM2 的计数时钟。
 * APB1 预分频不为 1 时，定时器时钟是 PCLK1 的 2 倍。
 * 72MHz 下 APB1=/2，PCLK1=36MHz，TIM2CLK=72MHz。
 */
static uint32_t timer_clock_hz(void)
{
    return BOARD_SYSCLK_HZ;
}
/*
 * 把 s_pixels 编码成 DMA 的 CCR 序列。
 * WS2812B 的传输顺序是 G -> R -> B，每个字节高位先发。
 * 最后补 64 个 0，形成帧尾低电平。
 */
static void encode_frame(void)
{
    uint16_t pos = 0U;
    uint16_t led;
    uint8_t  channel;
    uint8_t  bit;

    for (led = 0U; led < (uint16_t)WS2812_LED_COUNT; led++) {
        /* WS2812B receives G, R, B in that order, MSB first. */
        uint8_t grb[3];
//RGB转GRB
        grb[0] = s_pixels[led][1];//G R
        grb[1] = s_pixels[led][0];//R G
        grb[2] = s_pixels[led][2];//B B
//一颗灯珠内
        for (channel = 0U; channel < 3U; channel++) {
//从高位依次拿      
			for (bit = 0U; bit < 8U; bit++) {
                if ((grb[channel] & (uint8_t)(0x80U >> bit)) != 0U) {
                    s_frame[pos] = s_ccr_one;//58
                } else {
                    s_frame[pos] = s_ccr_zero;//29
                }
                pos++;
            }
        }
    }
//末尾补零
    while (pos < (uint16_t)WS2812_FRAME_WORDS) {
        s_frame[pos] = 0U;
        pos++;
    }
}

/*
 * 初始化灯带驱动：
 *   1. 开 GPIOA/TIM2/DMA1 时钟；
 *   2. PA0 配成 TIM2_CH1 复用推挽；
 *   3. TIM2 设 800kHz，PWM 模式 1，CC1 预装载；
 *   4. DMA1_Channel5 配成 16 位、内存递增、内存→外设；
 *   5. 先发一帧全灭，避免上电随机亮。
 */

    void ws2812_init(void)
{
    GPIO_InitTypeDef        gpio;
    TIM_TimeBaseInitTypeDef tim;
    TIM_OCInitTypeDef       oc;
    DMA_InitTypeDef         dma;
	
    uint32_t   timclk;
    uint32_t   period;

    /* 1. 开时钟 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);
    
	RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);

    /* 2. PA0 = TIM2_CH1，复用推挽 */
    gpio.GPIO_Pin   = GPIO_Pin_0;
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);

    /* 3. 算 800kHz 的周期 */
    period = 90U;       /* 72MHz / 800kHz = 90 */
    s_ccr_zero = 29U;   /* 90 × 32% ≈ 29 */
    s_ccr_one  = 58U;   /* 90 × 64% ≈ 58 */

    /* 4. TIM2 时基：800kHz */
    TIM_TimeBaseStructInit(&tim);
    tim.TIM_Prescaler         = 0;//不分频，定时器时钟=72MHZ PSC
    tim.TIM_CounterMode       = TIM_CounterMode_Up;//向上计数
    tim.TIM_Period            = (uint16_t)(period - 1U);//  ARR
    tim.TIM_ClockDivision     = TIM_CKD_DIV1;//无额外分频
    tim.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM2, &tim);

    /* 5. TIM2_CH1 PWM 模式 1 */
    TIM_OCStructInit(&oc);
    oc.TIM_OCMode      = TIM_OCMode_PWM1;//PWM 模式 1 CNT<CRR有效
    oc.TIM_OutputState = TIM_OutputState_Enable;//使能输出
    oc.TIM_Pulse       = 0;//初始比较值0
    oc.TIM_OCPolarity  = TIM_OCPolarity_High;//高电平有效
    TIM_OC1Init(TIM2, &oc);
	
    TIM_OC1PreloadConfig(TIM2, TIM_OCPreload_Enable);//比较值缓冲
	TIM_ARRPreloadConfig(TIM2, ENABLE);//ARR重装值缓冲

    /* 6. DMA1_Channel5：内存 -> TIM2->CCR1 */
    DMA_StructInit(&dma);
    dma.DMA_PeripheralBaseAddr = (uint32_t)(uintptr_t)&TIM2->CCR1;
	dma.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
	dma.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;
    dma.DMA_MemoryBaseAddr     = (uint32_t)(uintptr_t)s_frame;
	dma.DMA_MemoryDataSize     = DMA_MemoryDataSize_HalfWord;
    dma.DMA_MemoryInc          = DMA_MemoryInc_Enable;//依次传输下一数据
    dma.DMA_DIR                = DMA_DIR_PeripheralDST;//存储器到外设
    dma.DMA_BufferSize         = WS2812_FRAME_WORDS;
    dma.DMA_Mode               = DMA_Mode_Normal;//传输模式
    dma.DMA_Priority           = DMA_Priority_High;//高优先级
    dma.DMA_M2M                = DMA_M2M_Disable;//硬件
    DMA_Init(DMA1_Channel5, &dma);
	
    DMA_Cmd(DMA1_Channel5, DISABLE);//先失能调用cmd

    /* 7. 开 TIM2 的 CC1 DMA 请求 */
    TIM_DMACmd(TIM2, TIM_DMA_CC1, ENABLE);//开比较
    TIM_Cmd(TIM2, DISABLE);//关定时器

    /* 8. 先输出低电平，清标志 */
    TIM_SetCompare1(TIM2, 0U);
    TIM_GenerateEvent(TIM2, TIM_EventSource_Update);
    TIM_ClearFlag(TIM2, TIM_FLAG_Update);

    /* 9. 上电先全灭 */
    ws2812_fill(0U, 0U, 0U);
    ws2812_show();
}
/*
 * 设置一颗灯的颜色。
 * index 从 0 开始；r/g/b 为 0~255。
 * WS2812_REVERSE=1 时，索引会反过来映射，用于灯带装反的情况。
 */
void ws2812_set_pixel(uint16_t index, uint8_t r, uint8_t g, uint8_t b)
{
    if (index >= (uint16_t)WS2812_LED_COUNT) {
        return;
    }

#if WS2812_REVERSE
    index = (uint16_t)(WS2812_LED_COUNT - 1U - index);
#endif

    s_pixels[index][0] = r;
    s_pixels[index][1] = g;
    s_pixels[index][2] = b;
}

/* 整条灯带填充同一种颜色 */
void ws2812_fill(uint8_t r, uint8_t g, uint8_t b)
{
    uint16_t i;

    for (i = 0U; i < (uint16_t)WS2812_LED_COUNT; i++) {
        ws2812_set_pixel(i, r, g, b);
    }
}

/*
 * 发送一帧：
 *   encode_frame() 生成 352 个 16 位 CCR 值；
 *   关 DMA、清标志、装源地址/长度、开 DMA；
 *   启动 TIM2，DMA 在每个 CC1 事件搬一个值；
 *   等 TCIF5 置位（或超时保护），然后停 TIM2、关 DMA。
 * 12 颗一帧约 0.45ms，阻塞等待。
 */
void ws2812_show(void)
{
    uint32_t guard = 500000UL;

    encode_frame();//里面每一个元素就是 PWM 的 CCR 比较值，只有三种：29（bit0）、58（bit1）、0（复位低电平）

    /* 1. 关 DMA，等它真的关掉 */
    DMA_Cmd(DMA1_Channel5, DISABLE);
    while ((DMA1_Channel5->CCR & 0x01U) != 0U) {}

    /* 2. 清标志，重装数据长度 */
    DMA_ClearFlag(DMA1_FLAG_TC5 | DMA1_FLAG_HT5 |
                  DMA1_FLAG_TE5 | DMA1_FLAG_GL5);//一次性清除 4 个不同的中断标志
    DMA_SetCurrDataCounter(DMA1_Channel5, WS2812_FRAME_WORDS);
    DMA_Cmd(DMA1_Channel5, ENABLE);

    /* 3. 启动 TIM2 */
    TIM_SetCounter(TIM2, 0U);
    TIM_SetCompare1(TIM2, 0U);
    TIM_ClearFlag(TIM2, TIM_FLAG_Update);
    TIM_Cmd(TIM2, ENABLE);

    /* 4. 等 DMA 传完 */
    while ((DMA_GetFlagStatus(DMA1_FLAG_TC5) == RESET) &&
           (guard > 0U)) {
        guard--;
    }

    /* 5. 收尾 */
    DMA_ClearFlag(DMA1_FLAG_TC5);
    DMA_Cmd(DMA1_Channel5, DISABLE);
    TIM_Cmd(TIM2, DISABLE);
    TIM_SetCompare1(TIM2, 0U);
    TIM_GenerateEvent(TIM2, TIM_EventSource_Update);
    TIM_SetCounter(TIM2, 0U);//CNT 清零，定时器归零，等待下一次 show 调用
}
