/**
 * @file ocr_rec_infer.c
 * @brief PP-OCRv4 en rec：裁剪 → 48×320 预处理 → NPU → CTC
 */

#include "ocr_rec_infer.h"
#include "ocr_rec_dict.h"
#include "ocr_display.h"
#include "ocr_dbg.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#if !OCR_REC_ENABLE

void ocr_rec_infer_init(void)
{
}

int ocr_rec_infer_boxes(const uint16_t *rgb565, uint16_t src_w, uint16_t src_h,
                        ocr_det_result_t *result)
{
  (void)rgb565;
  (void)src_w;
  (void)src_h;
  (void)result;
  return 0;
}

#else /* OCR_REC_ENABLE */

#include "ocr_infer.h"
#include "ocr_weights_reloc.h"
#include "ocr_rec.h"
#include "ll_aton_runtime.h"
#include "ll_aton_caches_interface.h"
#include "npu_cache.h"
#include <stdlib.h>

LL_ATON_DECLARE_NAMED_NN_INSTANCE_AND_INTERFACE(ocr_rec);

#define OCR_REC_ACT_BASE  0x34380000U

static uint8_t s_rec_hw_inited;
static int8_t s_rec_in_q[OCR_REC_HEIGHT * OCR_REC_WIDTH * 3U] __attribute__((aligned(32)));

static float ocr_rgb565_ch(uint16_t c, uint8_t ch)
{
  if (ch == 0U)
  {
    return ((float)((c >> 11) & 0x1FU) * 255.0f) / 31.0f;
  }
  if (ch == 1U)
  {
    return ((float)((c >> 5) & 0x3FU) * 255.0f) / 63.0f;
  }
  return ((float)(c & 0x1FU) * 255.0f) / 31.0f;
}

static int8_t ocr_rec_quant(float v)
{
  int q = (int)(v / OCR_REC_IN_SCALE + ((v >= 0.0f) ? 0.5f : -0.5f)) + OCR_REC_IN_ZP;
  if (q > 127)
  {
    return 127;
  }
  if (q < -128)
  {
    return (int8_t)-128;
  }
  return (int8_t)q;
}

static void ocr_rec_crop_axis(const uint16_t *rgb565, uint16_t src_w, uint16_t src_h,
                              const ocr_det_box_t *box, uint16_t *out_w, uint16_t *out_h,
                              uint16_t *crop_rgb)
{
  uint16_t x1 = box->x1;
  uint16_t y1 = box->y1;
  uint16_t x2 = box->x2;
  uint16_t y2 = box->y2;
  uint16_t cw;
  uint16_t ch;
  uint32_t y;
  uint32_t x;

  if (x2 < x1)
  {
    uint16_t t = x1;
    x1 = x2;
    x2 = t;
  }
  if (y2 < y1)
  {
    uint16_t t = y1;
    y1 = y2;
    y2 = t;
  }
  if (x2 >= src_w)
  {
    x2 = (uint16_t)(src_w - 1U);
  }
  if (y2 >= src_h)
  {
    y2 = (uint16_t)(src_h - 1U);
  }

  cw = (uint16_t)(x2 - x1 + 1U);
  ch = (uint16_t)(y2 - y1 + 1U);
  if ((cw < 2U) || (ch < 2U))
  {
    *out_w = 0U;
    *out_h = 0U;
    return;
  }

  for (y = 0U; y < ch; y++)
  {
    for (x = 0U; x < cw; x++)
    {
      crop_rgb[y * cw + x] = rgb565[(y1 + y) * src_w + (x1 + x)];
    }
  }
  *out_w = cw;
  *out_h = ch;
}

static int ocr_rec_preprocess_crop(const uint16_t *crop, uint16_t cw, uint16_t ch)
{
  float ratio;
  uint16_t resized_w;
  uint32_t plane = (uint32_t)OCR_REC_HEIGHT * (uint32_t)OCR_REC_WIDTH;
  int8_t *in_q = (int8_t *)(uintptr_t)OCR_REC_ACT_BASE;
  uint32_t dy;
  uint32_t dx;

  if ((crop == NULL) || (cw == 0U) || (ch == 0U))
  {
    return -1;
  }

  ratio = (float)cw / (float)ch;
  resized_w = (uint16_t)((float)OCR_REC_HEIGHT * ratio + 0.5f);
  if (resized_w < 1U)
  {
    resized_w = 1U;
  }
  if (resized_w > OCR_REC_WIDTH)
  {
    resized_w = OCR_REC_WIDTH;
  }

  memset(s_rec_in_q, 0, sizeof(s_rec_in_q));
  for (dy = 0U; dy < OCR_REC_HEIGHT; dy++)
  {
    float fy = ((float)dy + 0.5f) * (float)ch / (float)OCR_REC_HEIGHT - 0.5f;
    for (dx = 0U; dx < resized_w; dx++)
    {
      float fx = ((float)dx + 0.5f) * (float)cw / (float)resized_w - 0.5f;
      int32_t x0 = (int32_t)fx;
      int32_t y0 = (int32_t)fy;
      uint16_t c;
      float rf;
      float gf;
      float bf;
      uint32_t idx = dy * (uint32_t)OCR_REC_WIDTH + dx;

      if (x0 < 0)
      {
        x0 = 0;
      }
      if (y0 < 0)
      {
        y0 = 0;
      }
      if ((uint32_t)x0 >= cw)
      {
        x0 = (int32_t)cw - 1;
      }
      if ((uint32_t)y0 >= ch)
      {
        y0 = (int32_t)ch - 1;
      }
      c = crop[(uint32_t)y0 * cw + (uint32_t)x0];
      rf = ocr_rgb565_ch(c, 0U) / 255.0f;
      gf = ocr_rgb565_ch(c, 1U) / 255.0f;
      bf = ocr_rgb565_ch(c, 2U) / 255.0f;
      in_q[idx + 0U * plane] = ocr_rec_quant((rf - 0.5f) / 0.5f);
      in_q[idx + 1U * plane] = ocr_rec_quant((gf - 0.5f) / 0.5f);
      in_q[idx + 2U * plane] = ocr_rec_quant((bf - 0.5f) / 0.5f);
    }
  }

  SCB_CleanDCache_by_Addr((uint32_t *)in_q, (int32_t)LL_ATON_OCR_REC_IN_1_SIZE_BYTES);
  return 0;
}

static void ocr_rec_ctc_decode(const float *logits, uint32_t seq_len, uint32_t vocab,
                               char *out, size_t out_n, float *score_out)
{
  uint32_t t;
  int prev = -1;
  size_t pos = 0U;
  float conf_sum = 0.0f;
  uint32_t conf_cnt = 0U;

  if ((out == NULL) || (out_n == 0U))
  {
    return;
  }
  out[0] = '\0';
  if (score_out != NULL)
  {
    *score_out = 0.0f;
  }

  for (t = 0U; t < seq_len; t++)
  {
    const float *row = logits + t * vocab;
    int best = 0;
    float best_v = row[0];

    for (uint32_t c = 1U; c < vocab; c++)
    {
      if (row[c] > best_v)
      {
        best_v = row[c];
        best = (int)c;
      }
    }

    if ((best != 0) && (best != prev))
    {
      if ((best >= 0) && ((uint32_t)best < OCR_REC_DICT_SIZE))
      {
        const char *ch = ocr_rec_dict[best];
        size_t clen = strlen(ch);
        if ((pos + clen) < (out_n - 1U))
        {
          memcpy(out + pos, ch, clen);
          pos += clen;
          out[pos] = '\0';
        }
      }
      conf_sum += best_v;
      conf_cnt++;
    }
    prev = best;
  }

  if ((score_out != NULL) && (conf_cnt > 0U))
  {
    *score_out = conf_sum / (float)conf_cnt;
  }
}

static int ocr_rec_run_once(float *logits, uint32_t *seq_len, uint32_t *vocab)
{
  LL_ATON_RT_RetValues_t ret;
  uint32_t iter = 0U;

  LL_ATON_RT_RuntimeInit();
  if (ocr_rec_weights_load_to_hyperram() != 0)
  {
    LL_ATON_RT_RuntimeDeInit();
    return -1;
  }
  LL_ATON_RT_Init_Network(&NN_Instance_ocr_rec);

  do
  {
    ret = LL_ATON_RT_RunEpochBlock(&NN_Instance_ocr_rec);
    iter++;
    if (iter > 300U)
    {
      LL_ATON_RT_DeInit_Network(&NN_Instance_ocr_rec);
      LL_ATON_RT_RuntimeDeInit();
      return -1;
    }
  } while (ret != LL_ATON_RT_DONE);

  {
    const LL_Buffer_InfoTypeDef *obi = LL_ATON_Output_Buffers_Info_ocr_rec();
    const int8_t *out_q;
    uint32_t bytes;
    float *dst = logits;

    if ((obi == NULL) || (obi[0].name == NULL))
    {
      LL_ATON_RT_DeInit_Network(&NN_Instance_ocr_rec);
      LL_ATON_RT_RuntimeDeInit();
      return -1;
    }
    out_q = (const int8_t *)LL_Buffer_addr_start(&obi[0]);
    bytes = LL_ATON_OCR_REC_OUT_1_SIZE_BYTES;
    LL_ATON_Cache_MCU_Invalidate_Range((uintptr_t)out_q, bytes);
    *seq_len = OCR_REC_MAX_SEQ;
    *vocab = OCR_REC_DICT_SIZE;
    for (uint32_t i = 0U; i < bytes; i++)
    {
      dst[i] = OCR_REC_OUT_SCALE * ((float)out_q[i] - (float)OCR_REC_OUT_ZP);
    }
  }

  LL_ATON_RT_DeInit_Network(&NN_Instance_ocr_rec);
  LL_ATON_RT_RuntimeDeInit();
  return 0;
}

void ocr_rec_infer_init(void)
{
  if (s_rec_hw_inited != 0U)
  {
    return;
  }
  ocr_infer_init();
  s_rec_hw_inited = 1U;
}

int ocr_rec_infer_boxes(const uint16_t *rgb565, uint16_t src_w, uint16_t src_h,
                        ocr_det_result_t *result)
{
  static uint16_t crop_buf[640U * 120U];
  static float logits[OCR_REC_MAX_SEQ * OCR_REC_DICT_SIZE];
  int i;

  if ((rgb565 == NULL) || (result == NULL))
  {
    return -1;
  }

  ocr_rec_infer_init();
  SCB_InvalidateDCache_by_Addr((uint32_t *)(uintptr_t)rgb565,
                               (int32_t)((uint32_t)src_w * (uint32_t)src_h * 2U));

  for (i = 0; i < result->nb_box; i++)
  {
    uint16_t cw = 0U;
    uint16_t ch = 0U;
    char text[64];
    float score = 0.0f;
    uint32_t seq_len = 0U;
    uint32_t vocab = 0U;

    result->rec_text[i][0] = '\0';
    result->rec_score[i] = 0.0f;

    ocr_rec_crop_axis(rgb565, src_w, src_h, &result->boxes[i], &cw, &ch, crop_buf);
    if ((cw == 0U) || (ch == 0U))
    {
      continue;
    }
    if (ocr_rec_preprocess_crop(crop_buf, cw, ch) != 0)
    {
      continue;
    }
    if (ocr_rec_run_once(logits, &seq_len, &vocab) != 0)
    {
      continue;
    }
    ocr_rec_ctc_decode(logits, seq_len, vocab, text, sizeof(text), &score);
    if ((text[0] != '\0') && (score >= OCR_REC_DROP_SCORE))
    {
      (void)strncpy(result->rec_text[i], text, sizeof(result->rec_text[i]) - 1U);
      result->rec_text[i][sizeof(result->rec_text[i]) - 1U] = '\0';
      result->rec_score[i] = score;
    }
  }

  if (result->nb_box > 0)
  {
  (void)snprintf(result->result_text, sizeof(result->result_text), "%d: %s",
                   result->nb_box,
                   (result->rec_text[0][0] != '\0') ? result->rec_text[0] : "(det only)");
  }

  return 0;
}

#endif /* OCR_REC_ENABLE */
