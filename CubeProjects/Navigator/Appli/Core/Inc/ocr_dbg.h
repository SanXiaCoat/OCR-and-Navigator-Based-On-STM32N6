#ifndef OCR_DBG_H
#define OCR_DBG_H

#include "main.h"
#include "ocr_display.h"
#include "./RGBLCD/rgblcd.h"
#include <stdio.h>

/**
 * 调试信息：状态栏第 1 行（12px ASCII），第 2 行留给错误码。
 */
#define OCR_DBG(fmt, ...)                                                      \
  do                                                                           \
  {                                                                            \
    char _ocr_dbg_buf[80];                                                     \
    (void)snprintf(_ocr_dbg_buf, sizeof(_ocr_dbg_buf), fmt, ##__VA_ARGS__);    \
    ocr_display_status_line1(_ocr_dbg_buf, YELLOW);                            \
  } while (0)

#endif
