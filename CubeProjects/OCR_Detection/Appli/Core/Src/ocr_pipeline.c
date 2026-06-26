/**
 * @file ocr_pipeline.c
 * @brief KEY0 拍照 OCR
 */

#include "ocr_pipeline.h"
#include "./OV5640/ov5640_dcmipp.h"
#include "./RGBLCD/rgblcd.h"
#include "ocr_display.h"
#include "ocr_infer.h"
#include "ocr_rec_infer.h"
#include "ocr_weights_reloc.h"
#include "ocr_uart_dbg.h"
#include "ocr_dbg.h"
#include <stdio.h>
#include <string.h>

static uint8_t s_preview_hold;

/*
 * NPU/ATON 运行会破坏 pipeline 栈帧里保存的 result 指针（实测 NPU 后变成
 * 0x80808080）。把跨 NPU 运行需要用到的值存到 BSS 静态区（NPU 不触及），
 * 运行后再从这里重新加载，规避 callee-saved 寄存器/栈槽被破坏。
 */
static ocr_det_result_t *volatile s_pipe_result;
static volatile uint16_t s_pipe_w;
static volatile uint16_t s_pipe_h;
static volatile uint16_t s_pipe_x;
static volatile uint16_t s_pipe_y;

#define OCR_BOX_LINE_W  3U

uint8_t ocr_pipeline_preview_hold_get(void)
{
  return s_preview_hold;
}

void ocr_pipeline_preview_hold_set(uint8_t hold)
{
  s_preview_hold = (hold != 0U) ? 1U : 0U;
}

static void ocr_draw_box_thick(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2,
                               uint16_t color, uint8_t line_w)
{
  uint8_t t;

  if (x2 < x1)
  {
    uint16_t tmp = x1;
    x1 = x2;
    x2 = tmp;
  }
  if (y2 < y1)
  {
    uint16_t tmp = y1;
    y1 = y2;
    y2 = tmp;
  }

  for (t = 0U; t < line_w; t++)
  {
    uint16_t ox1 = (x1 > t) ? (uint16_t)(x1 - t) : x1;
    uint16_t oy1 = (y1 > t) ? (uint16_t)(y1 - t) : y1;
    uint16_t ox2 = (uint16_t)(x2 + t);
    uint16_t oy2 = (uint16_t)(y2 + t);

    rgblcd_draw_rectangle(ox1, oy1, ox2, oy2, color);
  }
}

void ocr_pipeline_show_result(const ocr_det_result_t *result,
                              uint16_t img_x, uint16_t img_y,
                              uint16_t img_w, uint16_t img_h)
{
  int i;
  static const uint16_t box_colors[] = { RED, GREEN, BLUE, YELLOW, CYAN, MAGENTA, BROWN, GRAY };

  if (result == NULL)
  {
    return;
  }

  for (i = 0; i < result->nb_box; i++)
  {
    const ocr_det_box_t *b = &result->boxes[i];
    uint16_t bx1 = (uint16_t)(img_x + b->x1);
    uint16_t by1 = (uint16_t)(img_y + b->y1);
    uint16_t bx2 = (uint16_t)(img_x + b->x2);
    uint16_t by2 = (uint16_t)(img_y + b->y2);
    uint16_t color = box_colors[i % (int)(sizeof(box_colors) / sizeof(box_colors[0]))];
    char idx_buf[8];

    ocr_draw_box_thick(bx1, by1, bx2, by2, color, OCR_BOX_LINE_W);
    (void)snprintf(idx_buf, sizeof(idx_buf), "#%d", i + 1);
    rgblcd_show_string(bx1, by1, 48U, 16U, 16U, idx_buf, color);
  }

  ocr_display_status_show(result->result_text, BLUE);
  ocr_display_debug_det_right(ocr_infer_get_dbg_stats());
  ocr_display_rec_text_right(result);
  ocr_display_show_preproc_left(img_x, img_y, img_w, img_h);
}

int ocr_pipeline_key0_capture(uint16_t img_w, uint16_t img_h,
                              uint16_t img_x, uint16_t img_y,
                              ocr_det_result_t *result)
{
  if (result == NULL)
  {
    return -1;
  }

  memset(result, 0, sizeof(*result));
  ocr_uart_set_enabled(0U);

  /* 跨 NPU 运行保存到 BSS，运行后重新加载 */
  s_pipe_result = result;
  s_pipe_w = img_w;
  s_pipe_h = img_h;
  s_pipe_x = img_x;
  s_pipe_y = img_y;

  ocr_uart_phase("cap");
  ov5640_dcmipp_stop();
  HAL_Delay(5);

  OCR_DBG("snap...");
  if (ov5640_dcmipp_snap(800U) != 0U)
  {
    ocr_uart_phase("cap snap FAIL");
    OCR_DBG("snap FAIL");
    (void)snprintf(result->result_text, sizeof(result->result_text),
                   "\xE6\x8B\x8D\xE7\x85\xA7\xE5\xA4\xB1\xE8\xB4\xA5");
    return -2;
  }

  ocr_uart_phase("cap snap OK");
  OCR_DBG("snap OK %ux%u", (unsigned)img_w, (unsigned)img_h);

  {
    uint32_t snap_bytes = (uint32_t)img_w * (uint32_t)img_h * 2U;

    SCB_InvalidateDCache_by_Addr((uint32_t *)ov5640_dcmipp_buf, (int32_t)snap_bytes);
  }

  /* 先推理再刷 LCD，避免 DMA2D 长时间阻塞且推理失败时仍能看到状态 */
  ocr_uart_phase("infer start");
  {
    int prep_rc;

    prep_rc = ocr_infer_session_begin();
    if (prep_rc == -2)
    {
      ocr_uart_phase("infer FAIL");
      (void)snprintf(result->result_text, sizeof(result->result_text),
                     "NPU\xE5\xA4\xB1\xE8\xB4\xA5:\xE6\x9C\xAA\xE7\x83\xA7\xE5\xBD\x95\xE6\x9D\x83\xE9\x87\x8D");
      return -3;
    }
    if (prep_rc != 0)
    {
      char err_short[24];

      ocr_weights_get_last_error(err_short, sizeof(err_short));
      (void)snprintf(result->result_text, sizeof(result->result_text), "%s", err_short);
      ocr_uart_phase("infer FAIL");
      return -3;
    }

    if (ocr_infer_session_feed((const uint16_t *)ov5640_dcmipp_buf, img_w, img_h) != 0)
    {
      ocr_infer_session_end();
      (void)snprintf(result->result_text, sizeof(result->result_text),
                     "\xE9\xA2\x84\xE5\xA4\x84\xE7\x90\x86\xE5\xA4\xB1\xE8\xB4\xA5");
      ocr_uart_phase("infer FAIL");
      return -3;
    }

    ocr_uart_phase("npu start");
    if (ocr_infer_session_run() != 0)
    {
      ocr_infer_session_end();
      (void)snprintf(result->result_text, sizeof(result->result_text),
                     "NPU\xE5\xA4\xB1\xE8\xB4\xA5:\xE8\xB6\x85\xE6\x97\xB6");
      ocr_uart_phase("infer FAIL");
      return -3;
    }
    ocr_uart_phase("npu done");

    /* NPU 运行后从 BSS 恢复被破坏的指针/尺寸 */
    result = s_pipe_result;
    img_w = s_pipe_w;
    img_h = s_pipe_h;
    img_x = s_pipe_x;
    img_y = s_pipe_y;

    OCR_DBG("PRE r=%08lX", (unsigned long)(uintptr_t)result);
    ocr_uart_phase("POSTcall");
    if (ocr_infer_session_post(result, img_w, img_h) != 0)
    {
      ocr_infer_session_end();
      ocr_uart_phase("post FAIL");
      return -3;
    }

#if OCR_REC_ENABLE
    if (result->nb_box > 0)
    {
      ocr_uart_phase("rec");
      if (ocr_rec_infer_boxes((const uint16_t *)ov5640_dcmipp_buf, img_w, img_h, result) != 0)
      {
        ocr_display_status_line2("rec FAIL", RED);
      }
    }
#endif

    ocr_uart_dump_det_report(0, result, img_w, img_h);
    rgblcd_fill(0U, 0U, 24U, 24U, GREEN);
    ocr_infer_session_end();
  }

  /* teardown 后再次从 BSS 恢复，防止 ATON 关闭过程再破坏寄存器/栈 */
  result = s_pipe_result;
  img_w = s_pipe_w;
  img_h = s_pipe_h;
  img_x = s_pipe_x;
  img_y = s_pipe_y;

  rgblcd_color_fill(img_x, img_y, (uint16_t)(img_x + img_w - 1U),
                    (uint16_t)(img_y + img_h - 1U),
                    (uint16_t *)ov5640_dcmipp_buf);
  ocr_pipeline_show_result(result, img_x, img_y, img_w, img_h);

  return 0;
}
