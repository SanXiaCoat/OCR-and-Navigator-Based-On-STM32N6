/**
 * @file ocr_uart_dbg.c
 * @brief KEY0 流程诊断：默认仅 LCD 状态栏，避免 USART 阻塞卡死。
 */

#include "ocr_uart_dbg.h"
#include "uart_test.h"
#include "ocr_display.h"
#include "./RGBLCD/rgblcd.h"
#include "main.h"
#include <stdio.h>
#include <string.h>

/* 仅 KEY2 显式 echo 会话时打开 UART 输出 */
static uint8_t s_uart_enabled;

void ocr_uart_set_enabled(uint8_t enabled)
{
  s_uart_enabled = (enabled != 0U) ? 1U : 0U;
}

uint8_t ocr_uart_is_enabled(void)
{
  return s_uart_enabled;
}

static void ocr_uart_line(const char *line)
{
  if ((line == NULL) || (s_uart_enabled == 0U))
  {
    return;
  }
  (void)uart_test_tx_line(line);
}

void ocr_uart_phase(const char *tag)
{
  char buf[48];

  if (tag == NULL)
  {
    return;
  }
  (void)snprintf(buf, sizeof(buf), "%s", tag);
  ocr_display_status_line1(buf, BLUE);
  if (s_uart_enabled != 0U)
  {
    char uart_buf[56];
    (void)snprintf(uart_buf, sizeof(uart_buf), "[OCR] %s", tag);
    ocr_uart_line(uart_buf);
  }
}

void ocr_uart_dump_det_report(int cap_rc, const ocr_det_result_t *result,
                              uint16_t src_w, uint16_t src_h)
{
  const ocr_det_dbg_t *dbg = ocr_infer_get_dbg_stats();
  char buf[80];

  (void)src_w;
  (void)src_h;
  (void)cap_rc;

  if (dbg != NULL)
  {
    (void)snprintf(buf, sizeof(buf), "npu %lums st%u nb%d",
                   (unsigned long)dbg->npu_ms, (unsigned)dbg->snap_step, dbg->nb_box);
    ocr_display_status_line2(buf, BLUE);
  }

  if ((result != NULL) && (result->result_text[0] != '\0'))
  {
    ocr_display_status_line1(result->result_text, BLUE);
  }

  if (s_uart_enabled == 0U)
  {
    return;
  }

  if (dbg != NULL)
  {
    (void)snprintf(buf, sizeof(buf),
                   "pk=%u ab=%u nb=%d cap=%d",
                   (unsigned)dbg->peak_milli, (unsigned)dbg->above_cnt, dbg->nb_box, cap_rc);
    ocr_uart_line(buf);
  }
}
