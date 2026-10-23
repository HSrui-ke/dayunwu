#ifndef APP_H
#define APP_H

#include <stdbool.h>
#include <stdint.h>

#include "board.h"

/*
 * ============================================================
 * app.h —— 状态机对外接口
 * ============================================================
 * app.c 只负责“业务逻辑”：串口问答、倒计时、状态切换；
 * 它不直接操作 GPIO/定时器，灯带颜色由 main.c 根据 app_get_visual()
 * 的返回值来渲染。
 */

/* 五个状态：绿待机 → 绿设置 → 黄等待 → 橙倒计时 → 橙闪烁 */
typedef enum {
    APP_STATE_GREEN_IDLE = 0,   /* 绿灯，等待串口发送 1 开始设置 */
    APP_STATE_GREEN_CONFIG,     /* 绿灯，正在问三个问题（保持绿色） */
    APP_STATE_YELLOW_WAIT_CAR,  /* 三项填完，变黄，等待小车进入 */
    APP_STATE_ORANGE_COUNTDOWN, /* 小车进入，橙色倒计时 */
    APP_STATE_ORANGE_BLINK      /* 倒计时结束车还在，橙色闪烁 */
} app_state_t;

/* 灯带颜色枚举，main.c 再映射成 RGB */
typedef enum {
    APP_COLOR_GREEN = 0,
    APP_COLOR_YELLOW,
    APP_COLOR_ORANGE,
    APP_COLOR_OFF
} app_color_t;

/* app_get_visual() 的输出：当前该显示什么 */
typedef struct {
    app_color_t color;      /* 整条灯带的主颜色 */
    bool        blink;      /* 是否处于闪烁状态 */
    bool        on;         /* 闪烁相位：true=亮，false=灭 */
    uint8_t     leds_off;   /* 从头部开始已经熄灭的灯珠数量 */
} app_visual_t;

/* 上电初始化，打印启动提示 */
void app_init(uint32_t now_ms);

/* 收到一整行串口数据（已去掉 CR/LF） */
void app_on_line(const char *line, uint32_t now_ms);

/* 小车稳定进入/离开事件；传感器层或 e/x 测试命令调用 */
void app_on_car_event(bool present, uint32_t now_ms);

/* 主循环每轮调用：每秒打印剩余时间、倒计时到点切闪烁 */
void app_poll(uint32_t now_ms);

/* 查询当前状态和总时长 */
app_state_t app_get_state(void);
uint32_t     app_get_total_ms(void);

/* 倒计时剩余毫秒；不在倒计时时返回 0 */
uint32_t     app_get_remaining_ms(uint32_t now_ms);

/* 根据当前状态和时间，计算灯带该怎么显示 */
void         app_get_visual(app_visual_t *out, uint32_t now_ms);

#endif /* APP_H */
