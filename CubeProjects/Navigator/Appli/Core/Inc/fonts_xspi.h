#ifndef __FONTS_XSPI_H
#define __FONTS_XSPI_H

#include "main.h"

/*
 * GBK font pack in XSPI2 NOR (memory-mapped). Must NOT overlap OCR det weights @ 0x71000000.
 * Weights end ~0x7115A080; pack starts at 0x71200000.
 * Build with tools/pack_fonts.py and flash via STM32CubeProgrammer.
 */
#define FONT_PACK_XSPI2_BASE    0x71200000U
#define FONT_PACK_MAGIC         0x46545046U  /* 'FTPF' */
#define FONT_PACK_VERSION       1U

uint8_t fonts_install_from_xspi(uint16_t x, uint16_t y, uint8_t size, uint16_t color);

#endif
