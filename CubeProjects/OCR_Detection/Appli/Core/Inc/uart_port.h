#ifndef __UART_PORT_H
#define __UART_PORT_H

#include "stm32n6xx_hal.h"

/**
 * 调试串口 USART3：PD8(TX) / PD9(RX)，115200 8N1
 * 外接 USB-TTL：TTL RX←PD8，TTL TX→PD9，GND 共地
 * 勿用 USART1@PE5/PE6（DCMIPP/板载 CH340 冲突）、UART4@PC10/11（板载占用）
 */
extern UART_HandleTypeDef huart3;

int uart_port_init(void);
UART_HandleTypeDef *uart_port_handle(void);

#endif
