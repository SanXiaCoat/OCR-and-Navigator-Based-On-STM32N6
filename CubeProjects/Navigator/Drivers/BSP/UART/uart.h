/**
 ****************************************************************************************************
 * @file        uart.h
 * @brief       串口驱动（仅用于无 DCMIPP 的实验，如 05_Serial）
 *
 * ATK-CNN647B 板载 CH340 接 USART1：PE5(TX) / PE6(RX)。
 * OV5640 DCMIPP 同样使用 PE5(D5)、PE6(D1) —— 与 USB-UART 硬件复用，不能同时使用。
 * 摄像头/OCR 工程请用 LCD 调试 (ocr_dbg.h)，运行期间勿插 USB-UART 线。
 ****************************************************************************************************
 */

#ifndef __UART_H
#define __UART_H

#include "main.h"
#include <stdio.h>

void uart_init(uint32_t baudrate);

/** 1=USART1(CH340 USB) 已初始化，可镜像 TX */
uint8_t uart_ch340_ready(void);
void uart_ch340_write(const uint8_t *data, uint16_t len);

#endif
