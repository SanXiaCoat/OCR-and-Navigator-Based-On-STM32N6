#ifndef __OCR_UART_DBG_H
#define __OCR_UART_DBG_H

#include "ocr_infer.h"
#include <stdint.h>

/**
 * KEY0 后可选通过 USART3(PD8/9) 输出诊断（115200 8N1）。
 * @param cap_rc ocr_pipeline_key0_capture 返回值
 */
void ocr_uart_dump_det_report(int cap_rc, const ocr_det_result_t *result,
                              uint16_t src_w, uint16_t src_h);

/** 轻量阶段标记（LCD 状态栏；UART 仅在 enable 时） */
void ocr_uart_phase(const char *tag);

/** KEY0 流程默认关闭 UART；KEY2 echo 会话可打开 */
void ocr_uart_set_enabled(uint8_t enabled);
uint8_t ocr_uart_is_enabled(void);

#endif /* __OCR_UART_DBG_H */
