/*
 * ============================================================
 * main.c —— 初始化和主循环
 * ============================================================
 * 上电流程：
 *   SysTick 1ms → USART1 9600 → WS2812 初始化 → app_init()
 *   → 渲染绿色 → while(1) 收串口行、跑 app_poll()、每 20ms 刷新灯带。
 *
 * 灯带颜色不直接在 app.c 里设置，而是 main.c 根据 app_get_visual()
 * 的 color/blink/leds_off 统一渲染。
 */

#include "app.h"
#include "board.h"
#include "console.h"
#include "stm32f10x.h"
#include "ws2812.h"

/* 1ms 时基，由 SysTick 中断累加 */
static volatile uint32_t s_millis;

/* SysTick 中断：只做最简单的自增 */
void SysTick_Handler(void)
{
    s_millis++;
}

/* 返回上电后的毫秒数（32 位读取在 Cortex-M3 上是原子的） */
static uint32_t millis(void)
{
    return s_millis;
}

/* 配置 SysTick 为 1ms 中断，优先级低于 USART1 */
static void time_init(void)
{
    SystemCoreClockUpdate();
    if (SysTick_Config(SystemCoreClock / 1000UL) != 0U) {
        while (1) {
            /* SysTick configuration failed */
        }
    }
    NVIC_SetPriority(SysTick_IRQn, 3U);
}

/* 把业务颜色枚举映射成具体的 RGB 值（board.h 里配置） */
static void color_to_rgb(app_color_t color, uint8_t *r, uint8_t *g, uint8_t *b)
{
    switch (color) {
    case APP_COLOR_GREEN:
        *r = COLOR_GREEN_R;
        *g = COLOR_GREEN_G;
        *b = COLOR_GREEN_B;
        break;
    case APP_COLOR_YELLOW:
        *r = COLOR_YELLOW_R;
        *g = COLOR_YELLOW_G;
        *b = COLOR_YELLOW_B;
        break;
    case APP_COLOR_ORANGE:
        *r = COLOR_ORANGE_R;
        *g = COLOR_ORANGE_G;
        *b = COLOR_ORANGE_B;
        break;
    case APP_COLOR_OFF:
    default:
        *r = 0U;
        *g = 0U;
        *b = 0U;
        break;
    }
}

/*
 * 渲染一帧灯带：
 *   - 从 app_get_visual() 取当前颜色/闪烁/已熄灭数量；
 *   - 闪烁时按 visual.on 决定整条亮还是灭；
 *   - APP_RENDER_PROGRESSIVE=1 时，橙色倒计时把前 leds_off 颗置黑；
 *   - 最后调用 ws2812_show() 真正发送。
 */
static void render_leds(uint32_t now_ms)
{
    app_visual_t visual;
    uint8_t      r;
    uint8_t      g;
    uint8_t      b;
    uint16_t     i;
    bool         output_on;

    app_get_visual(&visual, now_ms);
    color_to_rgb(visual.color, &r, &g, &b);
    output_on = (!visual.blink) || visual.on;

    for (i = 0U; i < (uint16_t)WS2812_LED_COUNT; i++) {
        bool led_on = output_on;

#if APP_RENDER_PROGRESSIVE
        if ((visual.color == APP_COLOR_ORANGE) &&
            (!visual.blink) &&
            (i < (uint16_t)visual.leds_off)) {
            led_on = false;
        }
#endif

        if (led_on) {
            ws2812_set_pixel(i, r, g, b);
        } else {
            ws2812_set_pixel(i, 0U, 0U, 0U);
        }
    }

    ws2812_show();
}

/* 主循环：串口命令、业务轮询、灯带刷新三件事 */
int main(void)
{
    char     line[64];
    uint32_t now;
    uint32_t last_render;

    time_init();

    console_init();
    ws2812_init();

    app_init(millis());
    render_leds(millis());
    last_render = millis();

    while (1) {
        now = millis();

        if (console_read_line(line, (uint16_t)sizeof(line))) {
            app_on_line(line, now);
            render_leds(now);
        }

        app_poll(now);

        if ((uint32_t)(now - last_render) >= APP_RENDER_PERIOD_MS) {
            last_render = now;
            render_leds(now);
        }
    }
}
