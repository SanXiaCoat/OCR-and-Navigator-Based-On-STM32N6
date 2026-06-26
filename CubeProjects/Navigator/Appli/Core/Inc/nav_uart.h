#ifndef __NAV_UART_H
#define __NAV_UART_H

#include <stdint.h>

void nav_uart_init(void);
void nav_uart_poll(void);
void nav_uart_redraw(void);

#endif /* __NAV_UART_H */
