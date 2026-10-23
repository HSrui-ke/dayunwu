#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/*
 * ============================================================
 * app_config.h —— 倒计时公式参数
 * ============================================================
 * 需求：
 *   1 人                = 15 秒
 *   每多 1 人           = +5 秒
 *   有行李              = +20 秒
 *   障碍模式            = 120 秒（覆盖人数/行李）
 *
 * 人数限制：1~4 人（CFG_MAX_PEOPLE）。
 */

/* 1 人的基础时长：15 秒 */
#define CFG_BASE_MS             15000UL

/* 每多 1 人增加的时长：5 秒 */
#define CFG_PER_PERSON_MS        5000UL

/* 有行李增加的时长：20 秒 */
#define CFG_LUGGAGE_MS          20000UL

/* 障碍模式的固定时长：120 秒 */
#define CFG_OBSTACLE_MS        120000UL

/* 人数上限：4 人（输入 5 及以上会提示无效） */
#define CFG_MAX_PEOPLE              4U

#endif /* APP_CONFIG_H */
