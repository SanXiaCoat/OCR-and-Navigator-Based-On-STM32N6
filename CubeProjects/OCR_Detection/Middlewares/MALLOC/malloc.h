/**
 ****************************************************************************************************
 * @file        malloc.h
 * @author      正点原子团队(ALIENTEK)
 * @version     V1.0
 * @date        2025-01-13
 * @brief       内存管理驱动代码
 * @license     Copyright (c) 2020-2032, 广州市星翼电子科技有限公司
 ****************************************************************************************************
 */

#ifndef __MALLOC_H
#define __MALLOC_H

#include "main.h"

#define SRAMIN      0
#define SRAMEX      1
#define SRAMBANK    2

#define MT_TYPE     uint32_t

#define MEM1_BLOCK_SIZE         (64)
/*
 * SRAMIN 内部堆：仅用于字库/文本临时分配（峰值几十 KB）。原基数 0x100000(1MB)
 * 严重超配，且其 mem1base 静态数组会延伸进 AI 激活区(0x34100000-0x34200000)。
 * 改为 0x20000(128KB)，给应用 RAM 留在低 1MB、与激活区不重叠。
 */
#define MEM1_MAX_SIZE           ((0x00020000 / (MEM1_BLOCK_SIZE + sizeof(MT_TYPE))) * MEM1_BLOCK_SIZE)
#define MEM1_ALLOC_TABLE_SIZE   (MEM1_MAX_SIZE / MEM1_BLOCK_SIZE)

#define MEM2_BLOCK_SIZE         (64)
/* HyperRAM 32MB 中预留：LTDC + DCMIPP + ocr_det 权重池(.ocr_wgt，与 OCR_DET_WEIGHTS_BYTES 一致) */
#define MEM2_OCR_WGT_BYTES      0x15A080UL
#define MEM2_EXTRAM_RESERVED    ((2UL * 1280UL * 800UL) + (2UL * 1024UL * 1024UL) + MEM2_OCR_WGT_BYTES)
#define MEM2_MAX_SIZE           (((0x02000000UL - MEM2_EXTRAM_RESERVED) / (MEM2_BLOCK_SIZE + sizeof(MT_TYPE))) * MEM2_BLOCK_SIZE)
#define MEM2_ALLOC_TABLE_SIZE   (MEM2_MAX_SIZE / MEM2_BLOCK_SIZE)

struct _m_mallco_dev
{
    void (*init)(uint8_t);
    uint16_t (*perused)(uint8_t);
    uint8_t *membase[SRAMBANK];
    MT_TYPE *memmap[SRAMBANK];
    uint8_t  memrdy[SRAMBANK];
};

extern struct _m_mallco_dev mallco_dev;

void my_mem_init(uint8_t memx);
uint16_t my_mem_perused(uint8_t memx);
void my_mem_set(void *s, uint8_t c, uint32_t count);
void my_mem_copy(void *des, void *src, uint32_t n);
void myfree(uint8_t memx, void *ptr);
void *mymalloc(uint8_t memx, uint32_t size);
void *myrealloc(uint8_t memx, void *ptr, uint32_t size);

#endif
