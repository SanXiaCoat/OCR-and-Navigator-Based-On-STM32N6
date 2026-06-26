#ifndef __UART_TEST_H
#define __UART_TEST_H

#include <stdint.h>

/** 绑定 UART4，发送欢迎信息 */
int uart_test_start(void);

/** 主循环中调用：收到一字节则回显 */
void uart_test_poll_echo(void);

/**
 * 阻塞回显测试（适合 KEY2 触发）
 * @param duration_ms 最长持续时间，0=一直回显直到无输入超时 60s
 * @return 回显字节数
 */
uint32_t uart_test_echo_session(uint32_t duration_ms);

/** 发送固定自检字符串并等待 "OK" 回环（单机上仅测 TX） */
int uart_test_tx_line(const char *line);

#endif
