#ifndef BOARD_H
#define BOARD_H

/*
 * ============================================================
 * board.h —— 硬件和功能配置
 * ============================================================
 * 目标芯片：STM32F103C8T6，72MHz
 * 灯带    ：12 颗 WS2812B，数据脚 PA0 / TIM2_CH1 / DMA1_Channel5
 * 串口    ：USART1，PA9=TX，PA10=RX，9600 8N1
 *
 * 这个文件只放“可以改的配置”。如果换引脚，除了改这里，
 * 还要改 ws2812.c / console.c 里对应的定时器、DMA、USART 初始化。
 */

/* 系统时钟，单位 Hz。代码里主要用 SystemCoreClock 计算，这里仅作说明。 */
#define BOARD_SYSCLK_HZ         72000000UL

/* 串口波特率。9600 和桌面的 9-1 串口例程一致；可改 115200。 */
#define BOARD_UART_BAUD         9600UL

/* 灯带灯珠数量。当前 12 颗；如果改成 60 颗，计时和缓冲区也会跟着变。 */
#define WS2812_LED_COUNT        12

/* 灯带方向。0=索引 0 在 DIN 端；1=整条反过来（灯带装反时用）。 */
#define WS2812_REVERSE          0

/*
 * 全局亮度 0~255。
 * 5V 供电时 12 颗全白约 720mA；默认 64 比较安全。
 * 用 STM32 的 5V 脚供电时，不要长时间设为 255。
 */
#define WS2812_BRIGHTNESS       64

/* 状态机用的颜色，已经按亮度缩放。WS2812B 内部是 GRB 顺序。 */
#define COLOR_GREEN_R           0
#define COLOR_GREEN_G           WS2812_BRIGHTNESS
#define COLOR_GREEN_B           0

#define COLOR_YELLOW_R          WS2812_BRIGHTNESS
#define COLOR_YELLOW_G          (WS2812_BRIGHTNESS * 3 / 4)
#define COLOR_YELLOW_B          0

#define COLOR_ORANGE_R          WS2812_BRIGHTNESS
#define COLOR_ORANGE_G          (WS2812_BRIGHTNESS / 4)
#define COLOR_ORANGE_B          0

/*
 * 1 = 倒计时时 12 颗逐颗熄灭（正常模式）
 * 0 = 整条同色，只测试绿/黄/橙（第一次接线时可以用）
 */
#define APP_RENDER_PROGRESSIVE  1

/* 超时后橙色闪烁：250ms 亮、250ms 灭，即 2Hz。 */
#define APP_BLINK_HALF_MS       250UL

/* 主循环刷新灯带的周期。一帧 WS2812 约 0.45ms。 */
#define APP_RENDER_PERIOD_MS    20UL

/* 串口回显：1=回显输入字符和退格。 */
#define CONSOLE_ECHO            1

/*
 * 文本编码：统一使用 UTF-8。
 * 源文件、Keil 编辑器、串口助手都设为 UTF-8。
 * 提示字符串在 texts.c / texts.h 中是普通 UTF-8 字符串。
 */

#endif /* BOARD_H */
