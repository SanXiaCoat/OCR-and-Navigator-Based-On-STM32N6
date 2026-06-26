/**
 * @file ocr_uart_dbg.c
 * @brief KEY0 后通过 UART4 输出 OCR 检测诊断（便于与 Python 对照）
 */

#include "ocr_uart_dbg.h"
#include "uart_test.h"
#include "ocr_display.h"
#include "main.h"
#include <stdio.h>
#include <string.h>

static void ocr_uart_line(const char *line)
{
  if (line == NULL)
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
  (void)snprintf(buf, sizeof(buf), "[OCR] %s", tag);
  ocr_uart_line(buf);
}

static uint8_t ocr_rgb565_luma(uint16_t c)
{
  uint8_t r = (uint8_t)((c >> 11) & 0x1FU);
  uint8_t g = (uint8_t)((c >> 5) & 0x3FU);
  uint8_t b = (uint8_t)(c & 0x1FU);

  r = (uint8_t)((r << 3) | (r >> 2));
  g = (uint8_t)((g << 2) | (g >> 4));
  b = (uint8_t)((b << 3) | (b >> 2));
  return (uint8_t)((77U * r + 150U * g + 29U * b) >> 8);
}

/** 3x3 Laplacian 方差代理（越大越清晰） */
static uint32_t ocr_preproc_sharpness(const uint16_t *rgb, uint16_t w, uint16_t h)
{
  uint32_t sum = 0U;
  uint32_t cnt = 0U;
  uint32_t y;
  uint32_t x;

  if ((rgb == NULL) || (w < 3U) || (h < 3U))
  {
    return 0U;
  }

  for (y = 1U; y < (uint32_t)h - 1U; y++)
  {
    for (x = 1U; x < (uint32_t)w - 1U; x++)
    {
      uint32_t idx = y * (uint32_t)w + x;
      int32_t c = (int32_t)ocr_rgb565_luma(rgb[idx]);
      int32_t lap = 4 * c
                    - (int32_t)ocr_rgb565_luma(rgb[idx - 1U])
                    - (int32_t)ocr_rgb565_luma(rgb[idx + 1U])
                    - (int32_t)ocr_rgb565_luma(rgb[idx - (uint32_t)w])
                    - (int32_t)ocr_rgb565_luma(rgb[idx + (uint32_t)w]);
      uint32_t a = (lap < 0) ? (uint32_t)(-lap) : (uint32_t)lap;

      sum += a;
      cnt++;
    }
  }

  return (cnt > 0U) ? (sum / cnt) : 0U;
}

static void ocr_uart_dump_heatmap_ascii(float thresh)
{
  const float *heat = ocr_infer_get_heatmap(NULL);
  char line[48];
  uint32_t gy;

  if (heat == NULL)
  {
    return;
  }

  ocr_uart_line("--- heat40 (0-9) ---");
  for (gy = 0U; gy < 40U; gy++)
  {
    uint32_t gx;
    char *p = line;

    for (gx = 0U; gx < 40U; gx++)
    {
      float v = heat[gy * 40U + gx];
      char ch;

      if (v < thresh)
      {
        ch = '.';
      }
      else if (v < thresh + 0.05f)
      {
        ch = '1';
      }
      else if (v < thresh + 0.15f)
      {
        ch = '3';
      }
      else if (v < thresh + 0.30f)
      {
        ch = '5';
      }
      else if (v < thresh + 0.50f)
      {
        ch = '7';
      }
      else
      {
        ch = '9';
      }
      *p++ = ch;
    }
    *p = '\0';
    ocr_uart_line(line);
  }
}

void ocr_uart_dump_det_report(int cap_rc, const ocr_det_result_t *result,
                              uint16_t src_w, uint16_t src_h)
{
  const ocr_det_dbg_t *dbg = ocr_infer_get_dbg_stats();
  const ocr_focus_dbg_t *af = ocr_display_focus_get();
  const ocr_det_rej_t *rej = NULL;
  uint8_t rej_n = 0U;
  char buf[128];
  uint16_t pw;
  uint16_t ph;
  const uint16_t *pre = ocr_infer_get_preproc_rgb565(&pw, &ph);
  uint32_t sharp = 0U;
  int i;

  ocr_infer_get_rej_log(&rej, &rej_n);

  if (pre != NULL)
  {
    sharp = ocr_preproc_sharpness(pre, pw, ph);
  }

  ocr_uart_line("\r\n======== OCR DET REPORT ========");
  (void)snprintf(buf, sizeof(buf), "tick=%lu cap_rc=%d src=%ux%u",
                 (unsigned long)HAL_GetTick(), cap_rc, (unsigned)src_w, (unsigned)src_h);
  ocr_uart_line(buf);

  if (af != NULL)
  {
    (void)snprintf(buf, sizeof(buf),
                   "AF init=%u const=%u single=%u R29=0x%02X R23=0x%02X cmd3022=0x%02X",
                   (unsigned)af->init_rc, (unsigned)af->const_rc, (unsigned)af->single_rc,
                   (unsigned)af->st3029, (unsigned)af->st3023, (unsigned)af->cmd3022);
    ocr_uart_line(buf);
  }

  if (pre != NULL)
  {
    (void)snprintf(buf, sizeof(buf), "preproc=%ux%u sharp=%lu", (unsigned)pw, (unsigned)ph,
                   (unsigned long)sharp);
    ocr_uart_line(buf);
  }

  if (dbg != NULL)
  {
    (void)snprintf(buf, sizeof(buf),
                   "in_q min=%d max=%d mean_m=%d npu_ms=%lu snap_st=%u",
                   (int)dbg->in_q_min, (int)dbg->in_q_max, (int)dbg->in_q_mean_milli,
                   (unsigned long)dbg->npu_ms, (unsigned)dbg->snap_step);
    ocr_uart_line(buf);

    (void)snprintf(buf, sizeof(buf),
                   "map_valid=%u pk=%u mn=%u mn_h=%u hot60=%u max_q=%d",
                   (unsigned)dbg->map_valid, (unsigned)dbg->peak_milli,
                   (unsigned)dbg->heat_mean_milli, (unsigned)dbg->heat_min_milli,
                   (unsigned)dbg->hot_cnt, (int)dbg->max_q);
    ocr_uart_line(buf);

    (void)snprintf(buf, sizeof(buf),
                   "th=%u bt=%u ab=%u fl=%u ma=%u rj_s=%u rj_sc=%u nb=%d",
                   (unsigned)dbg->thresh_milli, (unsigned)dbg->box_thresh_milli,
                   (unsigned)dbg->above_cnt, (unsigned)dbg->flood_cnt, (unsigned)dbg->max_area,
                   (unsigned)dbg->rej_small, (unsigned)dbg->rej_score, dbg->nb_box);
    ocr_uart_line(buf);
  }

  if ((result != NULL) && (result->result_text[0] != '\0'))
  {
    (void)snprintf(buf, sizeof(buf), "status: %s", result->result_text);
    ocr_uart_line(buf);
  }

  for (i = 0; i < (int)OCR_DET_REJ_LOG; i++)
  {
    if ((rej == NULL) || (i >= (int)rej_n))
    {
      break;
    }
    (void)snprintf(buf, sizeof(buf), "rej%d %s ar=%u %ux%u sc=%u",
                   i + 1, (rej[i].reason == 1U) ? "small" : "score", (unsigned)rej[i].area,
                   (unsigned)rej[i].w, (unsigned)rej[i].h, (unsigned)rej[i].score_milli);
    ocr_uart_line(buf);
  }

  if ((result != NULL) && (dbg != NULL))
  {
    for (i = 0; i < result->nb_box; i++)
    {
      const ocr_det_box_t *b = &result->boxes[i];

      (void)snprintf(buf, sizeof(buf), "box%d src[%u,%u,%u,%u] sc=%u", i + 1,
                     (unsigned)b->x1, (unsigned)b->y1, (unsigned)b->x2, (unsigned)b->y2,
                     (unsigned)dbg->box_score_milli[i]);
      ocr_uart_line(buf);
    }
  }

  ocr_uart_dump_heatmap_ascii((dbg != NULL) ? ((float)dbg->thresh_milli / 1000.0f) : 0.30f);
  ocr_uart_line("======== END REPORT ==========\r\n");
}
