#ifndef OCR_REC_INFER_H
#define OCR_REC_INFER_H

#include "ocr_infer.h"
#include <stdint.h>

/**
 * 0 = det-only（无 ocr_rec.c 时默认）
 * 1 = 级联 rec（须 CubeMX Generate ocr_rec + 烧录 @0x71600000）
 */
#ifndef OCR_REC_ENABLE
#define OCR_REC_ENABLE 0
#endif

#if OCR_REC_ENABLE
#define OCR_REC_WEIGHTS_XSPI2_BASE   0x71600000U
/* Generate 后按 report 更新 */
#define OCR_REC_WEIGHTS_BYTES        0x500000U
#define OCR_REC_HEIGHT               48U
#define OCR_REC_WIDTH                320U
#define OCR_REC_IN_SCALE             0.003921568859368563f
#define OCR_REC_IN_ZP                (-128)
#define OCR_REC_OUT_SCALE            0.003921568859368563f
#define OCR_REC_OUT_ZP               (-128)
#define OCR_REC_MAX_SEQ              40U
#define OCR_REC_DROP_SCORE           0.5f
#endif

void ocr_rec_infer_init(void);

/** 对每个 det 框裁剪、rec 推理、CTC 解码，写入 result->rec_text[] */
int ocr_rec_infer_boxes(const uint16_t *rgb565, uint16_t src_w, uint16_t src_h,
                        ocr_det_result_t *result);

#endif /* OCR_REC_INFER_H */
