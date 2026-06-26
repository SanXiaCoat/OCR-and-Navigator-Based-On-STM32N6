#ifndef OCR_WEIGHTS_RELOC_H

#define OCR_WEIGHTS_RELOC_H



#include <stddef.h>

#include <stdint.h>

#include "ocr_rec_infer.h"



/** HyperRAM 权重镜像（链接段 .ocr_wgt，位于 LTDC/DCMIPP 与堆之间） */
extern uint8_t g_ocr_wgt_pool[];
#define OCR_WGT_RAM_BASE  ((uint32_t)(uintptr_t)g_ocr_wgt_pool)

#if OCR_REC_ENABLE
extern uint8_t g_ocr_rec_wgt_pool[];
#define OCR_REC_WGT_RAM_BASE  ((uint32_t)(uintptr_t)g_ocr_rec_wgt_pool)
int ocr_rec_weights_load_to_hyperram(void);
#endif

uintptr_t ocr_aton_phys_to_virt(uintptr_t address);



/** 间接读 NOR，把 ocr_det 权重拷到 HyperRAM；推理前调用一次 */

int ocr_weights_load_to_hyperram(void);



/** 短码 "WGT E3@0K" */
void ocr_weights_get_last_error(char *buf, size_t n);

/** 完整行 "WGT LOAD FAIL err=3 @0K" */
void ocr_weights_get_last_error_detail(char *buf, size_t n);



#endif

