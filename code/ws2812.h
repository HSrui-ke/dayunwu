#ifndef WS2812_H
#define WS2812_H

#include <stdint.h>

/*
 * ============================================================
 * ws2812.h —— WS2812B 灯带驱动
 * ============================================================
 * 12 颗灯，PA0 = TIM2_CH1，DMA1_Channel5 搬运每个 bit 的 CCR 值。
 * 调用顺序：
 *   ws2812_init();
 *   ws2812_fill(...) / ws2812_set_pixel(...);
 *   ws2812_show();   // 真正把数据发到灯带
 */

/* 初始化 GPIO/TIM2/DMA，并发送一帧全灭 */
void ws2812_init(void);

/* 设置某一颗的颜色，index 从 0 开始，r/g/b 为 0~255 */
void ws2812_set_pixel(uint16_t index, uint8_t r, uint8_t g, uint8_t b);

/* 整条灯带填充同一颜色 */
void ws2812_fill(uint8_t r, uint8_t g, uint8_t b);

/*
 * 发送一帧。12 颗约 0.45ms，函数会阻塞等待 DMA 完成。
 * 调用前要先 set_pixel/fill。
 */
void ws2812_show(void);

#endif /* WS2812_H */
