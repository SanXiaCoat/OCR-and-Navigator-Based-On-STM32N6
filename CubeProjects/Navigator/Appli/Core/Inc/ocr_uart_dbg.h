#ifndef __OCR_UART_DBG_H
#define __OCR_UART_DBG_H

#include "ocr_infer.h"
#include <stdint.h>

/**
 * KEY0 拍照后通过 UART4 输出完整 OCR 诊断报告（460800 8N1）。
 * @param cap_rc ocr_pipeline_key0_capture 返回值
 */
void ocr_uart_dump_det_report(int cap_rc, const ocr_det_result_t *result,
                              uint16_t src_w, uint16_t src_h);

/** 轻量阶段标记（KEY0 流程卡点诊断） */
void ocr_uart_phase(const char *tag);

#endif /* __OCR_UART_DBG_H */
