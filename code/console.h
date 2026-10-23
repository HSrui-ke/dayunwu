#ifndef CONSOLE_H
#define CONSOLE_H

#include <stdbool.h>
#include <stdint.h>

/*
 * ============================================================
 * console.h —— USART1 串口控制台
 * ============================================================
 * 9600 8N1，PA9=TX，PA10=RX。
 

/* 初始化 USART1、PA9/PA10、RX 中断 */
void console_init(void);


bool console_read_line(char *line, uint16_t max_len);


void console_print(const char *text);
void console_print_u32(uint32_t value);

#endif /* CONSOLE_H */
