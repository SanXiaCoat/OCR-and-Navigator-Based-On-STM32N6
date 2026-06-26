/**
 * @file ocr_pipeline.c
 * @brief KEY0 拍照 OCR
 */

#include "ocr_pipeline.h"
#include "./OV5640/ov5640_dcmipp.h"
#include "./RGBLCD/rgblcd.h"
#include "ocr_display.h"
#include "ocr_infer.h"
#include "ocr_weights_reloc.h"
#include "ocr_uart_dbg.h"
#include "ocr_dbg.h"
#include <stdio.h>
#include <string.h>

static uint8_t s_preview_hold;

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

  ocr_uart_phase("cap start");
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

  ocr_uart_phase("preproc start");
  OCR_DBG("preproc...");
  if (ocr_infer_preprocess_rgb565((const uint16_t *)ov5640_dcmipp_buf, img_w, img_h) != 0)
  {
    ocr_uart_phase("preproc FAIL");
    OCR_DBG("preproc FAIL");
    (void)snprintf(result->result_text, sizeof(result->result_text),
                   "\xE9\xA2\x84\xE5\xA4\x84\xE7\x90\x86\xE5\xA4\xB1\xE8\xB4\xA5");
    return -3;
  }
  ocr_uart_phase("preproc OK");
  ocr_display_show_preproc_left(img_x, img_y, img_w, img_h);

  ocr_uart_phase("infer start");
  OCR_DBG("infer...");
  if (ocr_infer_run_once() != 0)
  {
    char err_short[24];

    ocr_weights_get_last_error(err_short, sizeof(err_short));
    ocr_uart_phase("infer FAIL");
    OCR_DBG("infer FAIL");
    if (strncmp(err_short, "WGT OK", 6) != 0)
    {
      (void)snprintf(result->result_text, sizeof(result->result_text), "%s", err_short);
    }
    else
    {
      (void)snprintf(result->result_text, sizeof(result->result_text),
                     "NPU\xE5\xA4\xB1\xE8\xB4\xA5");
    }
    return -3;
  }
  ocr_uart_phase("infer OK");

  ocr_uart_phase("post start");
  OCR_DBG("post...");
  if (ocr_infer_postprocess(result, img_w, img_h) != 0)
  {
    ocr_uart_phase("post FAIL");
    OCR_DBG("post FAIL");
    return -3;
  }
  ocr_uart_phase("post OK");
  OCR_DBG("infer OK nb=%d", result->nb_box);

  OCR_DBG("blit...");
  rgblcd_color_fill(img_x, img_y, (uint16_t)(img_x + img_w - 1U),
                    (uint16_t)(img_y + img_h - 1U),
                    (uint16_t *)ov5640_dcmipp_buf);
  ocr_uart_phase("cap done");
  OCR_DBG("done");

  return 0;
}
