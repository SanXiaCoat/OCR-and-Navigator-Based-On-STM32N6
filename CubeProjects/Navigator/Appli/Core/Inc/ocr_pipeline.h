#ifndef __OCR_PIPELINE_H
#define __OCR_PIPELINE_H

#include "ocr_infer.h"

/** @brief 在预览图上画检测框并刷新 result_text */
void ocr_pipeline_show_result(const ocr_det_result_t *result,
                              uint16_t img_x, uint16_t img_y,
                              uint16_t img_w, uint16_t img_h);

/** @brief KEY0 识别后是否冻结预览（1=保持画框画面，不被 DCMIPP 覆盖） */
uint8_t ocr_pipeline_preview_hold_get(void);
void ocr_pipeline_preview_hold_set(uint8_t hold);

int ocr_pipeline_key0_capture(uint16_t img_w, uint16_t img_h,
                              uint16_t img_x, uint16_t img_y,
                              ocr_det_result_t *result);

#endif /* __OCR_PIPELINE_H */
