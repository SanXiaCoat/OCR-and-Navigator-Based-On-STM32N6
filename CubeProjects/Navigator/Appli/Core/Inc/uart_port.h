#ifndef __UART_PORT_H
#define __UART_PORT_H

#include "stm32n6xx_hal.h"
#include <stdint.h>

/**
 * CubeMX UART4：PC10(TX) / PC11(RX)，115200 8N1
 * 与板载 USB-CH340(USART1@PE5/PE6) 不是同一路，须外接 USB-TTL 到 PC10/PC11
 */
extern UART_HandleTypeDef huart4;

int uart_port_init(void);
UART_HandleTypeDef *uart_port_handle(void);
int uart_port_is_ready(void);
void uart_port_reinit(void);
int uart_port_tx(const uint8_t *data, uint16_t len, uint32_t timeout_ms);
int uart_port_tx_str(const char *s);
void uart_port_boot_banner(void);

#endif
