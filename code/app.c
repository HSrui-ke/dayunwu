/*
 * ============================================================
 * app.c —— 业务状态机
 * ============================================================
 * 负责：
 *   1. 串口三个问题的解析和校验
 *   2. 按人数/行李/障碍计算倒计时
 *   3. 绿 → 黄 → 橙 → 闪烁 → 绿 的状态切换
 *   4. 每秒打印剩余时间
 *
 * 不直接操作 GPIO/定时器；灯带颜色由 main.c 调用 app_get_visual()
 * 后渲染。传感器层以后只需要调用 app_on_car_event(true/false, now)。
 */

#include "app.h"
#include "app_config.h"
#include "console.h"
#include "texts.h"

/* 本次设置保存的三个答案和最终总时长 */
typedef struct {
    uint32_t people;
    bool     luggage;
    bool     obstacle;
    uint32_t total_ms;
} app_cfg_t;

/* 业务状态机的全局变量（单实例，静态） */
static app_state_t s_state;          /* 当前状态 */
static app_cfg_t   s_cfg;            /* 本次配置 */
static uint8_t     s_answer;         /* 当前问到第几题：0人数 1行李 2障碍 */
static uint32_t    s_countdown_start;/* 倒计时起点（ms） */
static uint32_t    s_blink_start;    /* 闪烁起点（ms） */
static uint32_t    s_last_report_sec;/* 上次打印剩余秒数，防止重复打印 */

/* 判断两个字符串是否完全相同 */
static bool str_eq(const char *a, const char *b)
{
    while ((*a != '\0') && (*b != '\0')) {
        if (*a != *b) {
            return false;
        }
        a++;
        b++;
    }

    return (*a == '\0') && (*b == '\0');
}



/* 解析十进制正整数；只接受 0~9，拒绝空串和字母 */
static bool parse_u32(const char *s, uint32_t *out)
{
    uint32_t value = 0;
    bool     any = false;

    while (*s != '\0') {
        if (*s < '0' || *s > '9') {
            return false;
        }
        any = true;
        if (value > 1000000UL) {
            return false;
        }
        value = value * 10UL + (uint32_t)(*s - '0');
        s++;
    }
    if (!any) {
        return false;
    }
    *out = value;
    return true;
}

/* 解析是/否：接受 1/0 */
static bool parse_yes_no(const char *s, bool *out)
{
    uint32_t value;

    if (parse_u32(s, &value)) {
        if (value > 1U) {
            return false;
        }
        *out = (value != 0U);
        return true;
    }
    
    
    return false;
}

/* 状态枚举 → 串口显示的中文状态名 */
static const char *state_text(app_state_t state)
{
    switch (state) {
    case APP_STATE_GREEN_IDLE:
        return TXT_STATE_GREEN;
    case APP_STATE_GREEN_CONFIG:
        return TXT_STATE_GREEN_CONFIG;
    case APP_STATE_YELLOW_WAIT_CAR:
        return TXT_STATE_YELLOW_WAIT;
    case APP_STATE_ORANGE_COUNTDOWN:
        return TXT_STATE_ORANGE;
    case APP_STATE_ORANGE_BLINK:
        return TXT_STATE_BLINK;
    default:
        return TXT_STATE_GREEN;
    }
}

/*
 * 计算总倒计时：
 *   障碍模式 = 120 秒，直接返回；
 *   否则 15 秒 + (人数-1)*5 秒 + 行李 20 秒。
 * 人数已经在输入时限制为 1~4。
 */
static uint32_t calculate_total_ms(void)
{
    uint32_t total;

    if (s_cfg.obstacle) {
        return CFG_OBSTACLE_MS;
    }

    total = CFG_BASE_MS;
    if (s_cfg.people > 1U) {
        total += (s_cfg.people - 1U) * CFG_PER_PERSON_MS;
    }
    if (s_cfg.luggage) {
        total += CFG_LUGGAGE_MS;
    }
    return total;
}

/* 三项填完后打印一行汇总，例如：人数=3，行李=有，障碍=否，倒计时=45秒 */
static void print_config_summary(void)
{
    console_print(TXT_SET_DONE);
    console_print(TXT_PEOPLE_IS);
    console_print_u32(s_cfg.people);

    if (s_cfg.luggage) {
        console_print(TXT_LUGGAGE_YES);
    } else {
        console_print(TXT_LUGGAGE_NO);
    }

    if (s_cfg.obstacle) {
        console_print(TXT_OBSTACLE_YES);
    } else {
        console_print(TXT_OBSTACLE_NO);
    }

    console_print(TXT_TOTAL_IS);
    console_print_u32(s_cfg.total_ms / 1000UL);
    console_print(TXT_SEC);
    console_print("\r\n");
}

/*
 * 小车进入后开始倒计时：
 *   1. 状态切到 ORANGE_COUNTDOWN；
 *   2. 记录开始时间；
 *   3. 串口打印“小车进入”和总秒数；
 *   4. 灯带由 main.c 根据 app_get_visual() 渲染成橙色。
 */
static void start_countdown(uint32_t now_ms)
{
    s_state = APP_STATE_ORANGE_COUNTDOWN;
    s_countdown_start = now_ms;
    s_last_report_sec = 0U;

    console_print(TXT_CAR_ENTER);
    console_print(TXT_COUNTDOWN_START);
    console_print_u32(s_cfg.total_ms / 1000UL);
    console_print(TXT_SEC);
    console_print("\r\n");
}

/*
 * 三个答案都有效后调用：
 *   计算总时长，切到“黄色等待小车”，打印汇总。
 * 注意：只有走到这里，灯带才从绿色变黄色。
 */
static void finish_config(uint32_t now_ms)
{
    s_cfg.total_ms = calculate_total_ms();
    s_state = APP_STATE_YELLOW_WAIT_CAR;

    print_config_summary();
    console_print(TXT_WAIT_CAR);

    /* All three answers are complete: only now does the strip turn yellow. */
    (void)now_ms;
}

/*
 * 收到绿灯状态下的 "1"：
 *   清空答案，进入绿色设置状态；
 *   一次性打印三个问题和输入提示，再打印一个 "> "。
 * 设置期间灯带保持绿色，不在这里变黄。
 */
static void start_config(uint32_t now_ms)
{
    (void)now_ms;

    s_cfg.people = 0U;
    s_cfg.luggage = false;
    s_cfg.obstacle = false;
    s_cfg.total_ms = 0U;
    s_answer = 0U;
    s_state = APP_STATE_GREEN_CONFIG;

    /* Show the complete form once, then accept the three answers in order. */
    console_print(TXT_START);
    console_print(TXT_PROMPT_PEOPLE);
    console_print("\r\n");
    console_print(TXT_PROMPT_LUGGAGE);
    console_print("\r\n");
    console_print(TXT_PROMPT_OBSTACLE);
    console_print("\r\n");
    console_print(TXT_INPUT_HINT);
    console_print(TXT_INPUT_PROMPT);
}

/*
 * 按 s_answer 处理当前问题：
 *   0 = 人数（1~4）
 *   1 = 是否有行李（1/0 或 y/n）
 *   2 = 是否障碍模式（1/0 或 y/n）
 * 输错只打印错误并重新给 "> "，不改变当前问题。
 */
static void handle_config_answer(const char *line, uint32_t now_ms)
{
    uint32_t value;
    bool     yes;

    switch (s_answer) {
    case 0U:
        if (!parse_u32(line, &value) || value < 1U || value > CFG_MAX_PEOPLE) {
            console_print(TXT_INVALID_PEOPLE);
            console_print(TXT_INPUT_PROMPT);
            return;
        }
        s_cfg.people = value;
        s_answer = 1U;
        console_print(TXT_INPUT_PROMPT);
        break;

    case 1U:
        if (!parse_yes_no(line, &yes)) {
            console_print(TXT_INVALID_YESNO);
            console_print(TXT_INPUT_PROMPT);
            return;
        }
        s_cfg.luggage = yes;
        s_answer = 2U;
        console_print(TXT_INPUT_PROMPT);
        break;

    case 2U:
        if (!parse_yes_no(line, &yes)) {
            console_print(TXT_INVALID_YESNO);
            console_print(TXT_INPUT_PROMPT);
            return;
        }
        s_cfg.obstacle = yes;
        finish_config(now_ms);
        break;

    default:
        s_answer = 0U;
        console_print(TXT_INPUT_PROMPT);
        break;
    }
}

/*
 * 串口命令 s：打印当前状态。
 * 倒计时中额外打印总时长和剩余秒数；闪烁时只打印总时长。
 */
static void print_status(uint32_t now_ms)
{
    console_print(TXT_STATUS);
    console_print(state_text(s_state));

    if ((s_state == APP_STATE_ORANGE_COUNTDOWN) ||
        (s_state == APP_STATE_ORANGE_BLINK)) {
        console_print(TXT_TOTAL);
        console_print_u32(s_cfg.total_ms / 1000UL); //总
        console_print(TXT_SEC);

        if (s_state == APP_STATE_ORANGE_COUNTDOWN) {
            uint32_t remaining_ms = app_get_remaining_ms(now_ms);
            uint32_t remaining_s = (remaining_ms + 999UL) / 1000UL;

            console_print(TXT_REMAINING);
            console_print_u32(remaining_s); //剩余
            console_print(TXT_SEC);
        }
    }

    console_print("\r\n");
}

/*
 * 串口命令 r：软件复位到绿灯待机。
 * 只清业务状态，不复位 MCU；灯带会在下一帧渲染成绿色。
 */
static void app_reset(void)
{
    s_state = APP_STATE_GREEN_IDLE;
    s_cfg.people = 0U;
    s_cfg.luggage = false;
    s_cfg.obstacle = false;
    s_cfg.total_ms = 0U;
    s_answer = 0U;
    s_countdown_start = 0U;
    s_blink_start = 0U;
    s_last_report_sec = 0U;

    console_print(TXT_RESET);
}

/* 上电初始化：清空所有业务变量，状态=绿灯待机，打印启动提示。 */
void app_init(uint32_t now_ms)
{
    (void)now_ms;

    s_state = APP_STATE_GREEN_IDLE;
    s_cfg.people = 0U;
    s_cfg.luggage = false;
    s_cfg.obstacle = false;
    s_cfg.total_ms = 0U;
    s_answer = 0U;
    s_countdown_start = 0U;
    s_blink_start = 0U;
    s_last_report_sec = 0U;

    console_print(TXT_BOOT);
}

/*
 * 串口一整行入口。
 * 处理顺序：
 *   1. ?/s/r 是全局命令，任何状态都能用；
 *   2. 绿色设置状态下，所有输入都当作三个问题的答案；
 *   3. e/x 用来模拟小车进入/离开；
 *   4. 绿灯状态下的 "1" 才开始设置。
 */
void app_on_line(const char *line, uint32_t now_ms)
{
   const char *cmd;

    if ((line == 0) || (*line == '\0')) {
        return;
    }

    cmd = line;

    if (str_eq(cmd, "?")) {
        console_print(TXT_HELP);                                          //
        if (s_state == APP_STATE_GREEN_CONFIG) {
            console_print(TXT_INPUT_PROMPT);
        }
        return;
    }
    if (str_eq(cmd, "s")) {
        print_status(now_ms);
        if (s_state == APP_STATE_GREEN_CONFIG) {
            console_print(TXT_INPUT_PROMPT);
        }
        return;
    }
    if (str_eq(cmd, "r")) {
        app_reset();
        return;
    }

    if (s_state == APP_STATE_GREEN_CONFIG) {   //设置状态
        handle_config_answer(cmd, now_ms);
        return;
    }

    if (str_eq(cmd, "e")) {
        if (s_state == APP_STATE_YELLOW_WAIT_CAR) {
            app_on_car_event(true, now_ms);
        } else if (s_state == APP_STATE_GREEN_IDLE) {
            console_print(TXT_NEED_START);
        }
        return;
    }

    if (str_eq(cmd, "x")) {
        app_on_car_event(false, now_ms);
        return;
    }

    if ((s_state == APP_STATE_GREEN_IDLE) && str_eq(cmd, "1")) {
        start_config(now_ms);
        return;
    }

    if (s_state == APP_STATE_GREEN_IDLE) {
        console_print(TXT_NEED_START);
    } else {
        console_print(TXT_UNKNOWN);
    }
}

/*
 * 小车存在状态变化。
 *   present=true 且正在“黄色等待小车” → 开始橙色倒计时；
 *   present=false 且在倒计时 → 提前离开，恢复绿灯；
 *   present=false 且在闪烁 → 离开，恢复绿灯。
 * 以后接 GY-273 时，把去抖后的稳定结果传进来即可。
 */
void app_on_car_event(bool present, uint32_t now_ms)
{
    if (present) {
        if (s_state == APP_STATE_YELLOW_WAIT_CAR) {
            start_countdown(now_ms);
        }
        return;
    }

    if (s_state == APP_STATE_ORANGE_COUNTDOWN) {
        s_state = APP_STATE_GREEN_IDLE;
        console_print(TXT_EARLY_LEAVE);
    } else if (s_state == APP_STATE_ORANGE_BLINK) {
        s_state = APP_STATE_GREEN_IDLE;
        console_print(TXT_LEAVE);
    }
}

/*
 * 主循环轮询：
 *   - 倒计时中每秒打印一次剩余秒数；
 *   - 到 0 时切到橙色闪烁，并打印超时提示。
 * 灯带颜色由 main.c 每 20ms 根据 app_get_visual() 刷新。
 */
void app_poll(uint32_t now_ms)
{
    uint32_t elapsed;
    uint32_t seconds;
    uint32_t remaining_ms;
    uint32_t remaining_s;

    if (s_state != APP_STATE_ORANGE_COUNTDOWN) {
        return;
    }

    elapsed = now_ms - s_countdown_start;//经过的

    if (elapsed >= s_cfg.total_ms) {
        s_state = APP_STATE_ORANGE_BLINK;
        s_blink_start = now_ms;
        console_print(TXT_TIMEOUT);
        return;
    }

    seconds = elapsed / 1000UL;
    if (seconds != s_last_report_sec) {
        s_last_report_sec = seconds;
        remaining_ms = s_cfg.total_ms - elapsed; //剩下的
        remaining_s = (remaining_ms + 999UL) / 1000UL; //向上取整，适合倒计时显示

        console_print(TXT_REMAIN);
        console_print_u32(remaining_s);
        console_print(TXT_REMAIN_SEC);
    }
}

/* 返回当前业务状态 */
app_state_t app_get_state(void)
{
    return s_state;
}

/* 返回本次配置的总倒计时（ms） */
uint32_t app_get_total_ms(void)
{
    return s_cfg.total_ms;
}

/* 倒计时剩余毫秒；不在倒计时或已到 0 时返回 0 */
uint32_t app_get_remaining_ms(uint32_t now_ms)
{
    uint32_t elapsed;

    if (s_state != APP_STATE_ORANGE_COUNTDOWN) {
        return 0U;
    }

    elapsed = now_ms - s_countdown_start;
    if (elapsed >= s_cfg.total_ms) {
        return 0U;
    }
    return s_cfg.total_ms - elapsed;
}

/*
 * 把当前状态翻译成“灯带怎么显示”：
 *   绿灯待机/设置 → 绿色；
 *   黄色等待      → 黄色；
 *   橙色倒计时    → 橙色 + leds_off（按 12 颗等分计算）；
 *   橙色闪烁      → 橙色，blink/on 控制 250ms 亮灭。
 */
void app_get_visual(app_visual_t *out, uint32_t now_ms)
{
    uint32_t elapsed;
    uint32_t off;

    if (out == 0) {
        return;
    }

    out->color = APP_COLOR_OFF;
    out->blink = false;
    out->on = true;
    out->leds_off = 0U;

    switch (s_state) {
    case APP_STATE_GREEN_IDLE:
        out->color = APP_COLOR_GREEN;
        break;

    case APP_STATE_GREEN_CONFIG:
        out->color = APP_COLOR_GREEN;
        break;

    case APP_STATE_YELLOW_WAIT_CAR:
        out->color = APP_COLOR_YELLOW;
        break;

    case APP_STATE_ORANGE_COUNTDOWN:
        out->color = APP_COLOR_ORANGE;
        if (s_cfg.total_ms > 0U) {
            elapsed = now_ms - s_countdown_start;
            off = (elapsed * (uint32_t)WS2812_LED_COUNT) / s_cfg.total_ms;
            if (off > (uint32_t)WS2812_LED_COUNT) {
                off = (uint32_t)WS2812_LED_COUNT;
            }
            out->leds_off = (uint8_t)off;
        }
        break;

    case APP_STATE_ORANGE_BLINK:
        out->color = APP_COLOR_ORANGE;
        out->blink = true;
        out->on = (( ((now_ms - s_blink_start) / APP_BLINK_HALF_MS) & 1U) == 0U);//按位与
        break;

    default:
        break;
    }
}

