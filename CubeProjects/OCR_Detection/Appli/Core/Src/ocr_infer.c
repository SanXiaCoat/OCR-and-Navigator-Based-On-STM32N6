/**

 * @file ocr_infer.c

 * @brief Appli 侧 ocr_det 推理：预处理 / NPU 推理 / 热力图后处理

 */

/* Override 10s ATON watchdog for 184 epochs */
#define ATON_EPOCH_TIMEOUT_MS 60000U


#include "ocr_infer.h"
#include "ocr_uart_dbg.h"
#include "ocr_weights_reloc.h"
#include "ocr_display.h"
#include "xspi2_nor.h"
#include "ocr_det.h"
#include "./RGBLCD/rgblcd.h"

#include "ll_aton_runtime.h"

#include "ll_aton.h"

#include "ll_aton_osal.h"

#include "ll_aton_caches_interface.h"

#include "npu_cache.h"

#include "main.h"

#include "ocr_dbg.h"

#include <stdio.h>

#include <string.h>



LL_ATON_DECLARE_NAMED_NN_INSTANCE_AND_INTERFACE(ocr_det);



/* 与 ocr_det.c buff_info 一致：Input/Output @ npuRAM5 0x342E0000 */

#define OCR_DET_ACT_BASE       0x342E0000U

#define OCR_FLOOD_QUEUE_CAP    (OCR_DET_MAP_W * OCR_DET_MAP_H)
#define OCR_MAP_VIS_BYTES      ((OCR_DET_MAP_W * OCR_DET_MAP_H + 7U) / 8U)

static uint8_t s_map_vis[OCR_MAP_VIS_BYTES] __attribute__((aligned(32)));

static uint16_t s_flood_queue[OCR_FLOOD_QUEUE_CAP] __attribute__((aligned(32)));

static float s_proc_heat[OCR_DET_HEAT_CELLS * OCR_DET_HEAT_CELLS] __attribute__((aligned(32)));

/* NPU 输出快照：纯 CPU 读写缓冲，不参与 DMA/NPU，避免对其做 DCache 维护 */
static int8_t s_out_q_snap[OCR_DET_MAP_W * OCR_DET_MAP_H] __attribute__((aligned(32)));
static int8_t s_last_max_q;
static uint16_t s_last_thresh_milli;
static uint16_t s_last_peak_milli;
static uint16_t s_last_snap_step;

static ocr_det_dbg_t s_det_dbg;
static ocr_det_rej_t s_rej_log[OCR_DET_REJ_LOG];
static uint8_t s_rej_n;

/* 双线性缩放到 160×160 的 RGB565（ImageNet 归一化 / INT8 量化之前） */
static uint16_t s_preproc_rgb565[OCR_DET_PREPROC_PIX] __attribute__((aligned(32)));



/* PP-OCR det 常用 ImageNet 归一化（ONNX 导出前预处理一致） */

#define OCR_NORM_MEAN_R        0.485f

#define OCR_NORM_MEAN_G        0.456f

#define OCR_NORM_MEAN_B        0.406f

#define OCR_NORM_STD_R         0.229f

#define OCR_NORM_STD_G         0.224f

#define OCR_NORM_STD_B         0.225f



/* PP-OCR DB 后处理参数（参考 Python ocr_pipeline.py） */
#define OCR_DB_THRESH          0.3f   /* 二值化阈值 */
#define OCR_DB_BOX_THRESH      0.2f   /* 框质量阈值（QDQ 160 用 0.2） */
#define OCR_DB_UNCLIP_RATIO    1.5f   /* Python unclip_ratio：pad = area*ratio/perimeter */
/* PaddleOCR 默认 cv2 解码为 BGR；ONNX 输入通道通常也是 B,G,R。 */
#define OCR_INPUT_ORDER_BGR    1U

/* 最近一次推理沿用的热力图阈值 */
static float s_heat_thresh = OCR_DB_THRESH;

static uint32_t s_aton_blk_idx;
static uint8_t s_npu_hw_inited;
static uint8_t s_npu_ready;
static uint32_t s_last_fail_blk;

static void ocr_infer_fpu_enable(void)
{
  SCB->CPACR |= (0xFUL << 20);
  __DSB();
  __ISB();
}

/* ATON 内部等待循环用的 watchdog（覆盖 ll_aton.c 中的 weak 空实现） */
static uint32_t s_aton_wdg_deadline_ms;

int startWatchdog(uint32_t timeout)
{
  (void)timeout;
  s_aton_wdg_deadline_ms = HAL_GetTick() + (uint32_t)ATON_EPOCH_TIMEOUT_MS;
  return 0;
}

int checkWatchdog(void)
{
  return (HAL_GetTick() >= s_aton_wdg_deadline_ms) ? 1 : 0;
}

int ocr_infer_weights_probe(uint32_t *w0, uint8_t *rd_out)
{
  uint8_t sample[8];
  uint32_t first;
  uint8_t rc;

  rc = xspi2_nor_read_cpu(OCR_DET_WEIGHTS_XSPI2_BASE, sample, sizeof(sample));
  if (rd_out != NULL)
  {
    *rd_out = rc;
  }

  if (rc != 0U)
  {
    if (w0 != NULL)
    {
      *w0 = 0U;
    }
    return -1;
  }

  first = (uint32_t)sample[0] | ((uint32_t)sample[1] << 8) | ((uint32_t)sample[2] << 16) |
          ((uint32_t)sample[3] << 24);
  if (w0 != NULL)
  {
    *w0 = first;
  }

  /* 未烧录时 NOR 擦除态为 0xFF；已烧 ocr_det_weights.hex 文件头为 0x00 */
  if (first == 0xFFFFFFFFU)
  {
    uint32_t w1;

    w1 = (uint32_t)sample[4] | ((uint32_t)sample[5] << 8) | ((uint32_t)sample[6] << 16) |
         ((uint32_t)sample[7] << 24);
    if (w1 == 0xFFFFFFFFU)
    {
      return 0;
    }
  }

  return 1;
}

int ocr_infer_weights_ok(void)
{
  uint8_t head[8];
  uint8_t mid[4];
  uint32_t a;
  uint32_t b;
  uint32_t c;
  uint8_t rc;

  rc = xspi2_nor_read_cpu(OCR_DET_WEIGHTS_XSPI2_BASE, head, sizeof(head));
  if (rc != 0U)
  {
    return -1;
  }

  rc = xspi2_nor_read_cpu(OCR_DET_WEIGHTS_XSPI2_BASE + 256U, mid, sizeof(mid));
  if (rc != 0U)
  {
    return -1;
  }

  a = (uint32_t)head[0] | ((uint32_t)head[1] << 8) | ((uint32_t)head[2] << 16) |
      ((uint32_t)head[3] << 24);
  b = (uint32_t)head[4] | ((uint32_t)head[5] << 8) | ((uint32_t)head[6] << 16) |
      ((uint32_t)head[7] << 24);
  c = (uint32_t)mid[0] | ((uint32_t)mid[1] << 8) | ((uint32_t)mid[2] << 16) |
      ((uint32_t)mid[3] << 24);

  if ((a == 0xFFFFFFFFU) && (b == 0xFFFFFFFFU) && (c == 0xFFFFFFFFU))
  {
    return 0;
  }
  return 1;
}

int ocr_infer_prepare(void)
{
  if (s_npu_ready != 0U)
  {
    return 0;
  }

  s_npu_ready = 1U;
  return 0;
}

static void ocr_set_clk_sleep_mode(void)

{

#if defined(CPU_IN_SECURE_STATE)

  __HAL_RCC_DBG_CLK_SLEEP_ENABLE();

#endif

  __HAL_RCC_XSPIPHYCOMP_CLK_SLEEP_ENABLE();

  __HAL_RCC_AXISRAM1_MEM_CLK_SLEEP_ENABLE();

  __HAL_RCC_AXISRAM2_MEM_CLK_SLEEP_ENABLE();

  __HAL_RCC_AXISRAM3_MEM_CLK_SLEEP_ENABLE();

  __HAL_RCC_AXISRAM4_MEM_CLK_SLEEP_ENABLE();

  __HAL_RCC_AXISRAM5_MEM_CLK_SLEEP_ENABLE();

  __HAL_RCC_AXISRAM6_MEM_CLK_SLEEP_ENABLE();

  __HAL_RCC_FLEXRAM_MEM_CLK_SLEEP_ENABLE();

  __HAL_RCC_CACHEAXIRAM_MEM_CLK_SLEEP_ENABLE();

#if defined(CPU_IN_SECURE_STATE)

  __HAL_RCC_RIFSC_CLK_SLEEP_ENABLE();

  __HAL_RCC_RISAF_CLK_SLEEP_ENABLE();

  __HAL_RCC_IAC_CLK_SLEEP_ENABLE();

#endif

  __HAL_RCC_XSPI1_CLK_SLEEP_ENABLE();

  __HAL_RCC_XSPI2_CLK_SLEEP_ENABLE();

  __HAL_RCC_CACHEAXI_CLK_SLEEP_ENABLE();

  __HAL_RCC_NPU_CLK_SLEEP_ENABLE();

}



void ocr_infer_init(void)

{
  if (s_npu_hw_inited != 0U)
  {
    return;
  }

  /* AXISRAM2~6 退出 retention，Activation 区在 0x342E0000 (AXISRAM5) */

  __HAL_RCC_AXISRAM1_MEM_CLK_ENABLE();

  __HAL_RCC_AXISRAM2_MEM_CLK_ENABLE();

  __HAL_RCC_AXISRAM3_MEM_CLK_ENABLE();

  __HAL_RCC_AXISRAM4_MEM_CLK_ENABLE();

  __HAL_RCC_AXISRAM5_MEM_CLK_ENABLE();

  __HAL_RCC_AXISRAM6_MEM_CLK_ENABLE();

  RAMCFG_SRAM1_AXI->CR &= ~RAMCFG_CR_SRAMSD;

  RAMCFG_SRAM2_AXI->CR &= ~RAMCFG_CR_SRAMSD;

  RAMCFG_SRAM3_AXI->CR &= ~RAMCFG_CR_SRAMSD;

  RAMCFG_SRAM4_AXI->CR &= ~RAMCFG_CR_SRAMSD;

  RAMCFG_SRAM5_AXI->CR &= ~RAMCFG_CR_SRAMSD;

  RAMCFG_SRAM6_AXI->CR &= ~RAMCFG_CR_SRAMSD;



  ocr_set_clk_sleep_mode();



  __HAL_RCC_NPU_CLK_ENABLE();

  __HAL_RCC_NPU_FORCE_RESET();

  __HAL_RCC_NPU_RELEASE_RESET();

  npu_cache_init();

  npu_cache_enable();
  npu_cache_invalidate();



  /* IAC：NPU 推理前须释放复位 */

  __HAL_RCC_IAC_CLK_ENABLE();

  __HAL_RCC_IAC_FORCE_RESET();

  __HAL_RCC_IAC_RELEASE_RESET();

  /* NPU0 中断优先级（RuntimeInit 内会 EnableIRQ） */
  LL_ATON_OSAL_SET_PRIORITY(0, 5U);

  s_npu_hw_inited = 1U;
}



static float ocr_rgb565_ch_f(uint16_t c, uint8_t ch)
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

static float ocr_bilinear_f(const uint16_t *rgb565, uint16_t src_w, uint16_t src_h, float fx,
                            float fy, uint8_t ch)
{
  int32_t x0;
  int32_t y0;
  int32_t x1;
  int32_t y1;
  float dx;
  float dy;
  float p00;
  float p10;
  float p01;
  float p11;

  if (fx < 0.0f)
  {
    fx = 0.0f;
  }
  if (fy < 0.0f)
  {
    fy = 0.0f;
  }
  if (fx > ((float)src_w - 1.0f))
  {
    fx = (float)src_w - 1.0f;
  }
  if (fy > ((float)src_h - 1.0f))
  {
    fy = (float)src_h - 1.0f;
  }

  x0 = (int32_t)fx;
  y0 = (int32_t)fy;
  x1 = (x0 + 1 < (int32_t)src_w) ? (x0 + 1) : x0;
  y1 = (y0 + 1 < (int32_t)src_h) ? (y0 + 1) : y0;
  dx = fx - (float)x0;
  dy = fy - (float)y0;

  p00 = ocr_rgb565_ch_f(rgb565[(uint32_t)y0 * src_w + (uint32_t)x0], ch);
  p10 = ocr_rgb565_ch_f(rgb565[(uint32_t)y0 * src_w + (uint32_t)x1], ch);
  p01 = ocr_rgb565_ch_f(rgb565[(uint32_t)y1 * src_w + (uint32_t)x0], ch);
  p11 = ocr_rgb565_ch_f(rgb565[(uint32_t)y1 * src_w + (uint32_t)x1], ch);

  return (1.0f - dx) * (1.0f - dy) * p00 + dx * (1.0f - dy) * p10 + (1.0f - dx) * dy * p01 +
         dx * dy * p11;
}

static uint8_t ocr_f32_to_u8(float v)
{
  if (v <= 0.0f)
  {
    return 0U;
  }
  if (v >= 255.0f)
  {
    return 255U;
  }
  return (uint8_t)(v + 0.5f);
}



static float ocr_norm_channel(uint8_t v, float mean, float std)

{

  return (((float)v / 255.0f) - mean) / std;

}



static int8_t ocr_quant_input(float v)

{

  int q = (int)(v / OCR_DET_IN_SCALE + ((v >= 0.0f) ? 0.5f : -0.5f)) + OCR_DET_IN_ZP;

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



static float ocr_dequant_output(int8_t q)

{

  return OCR_DET_OUT_SCALE * ((float)q - (float)OCR_DET_OUT_ZP);

}



static int8_t ocr_quant_thresh(float t)

{

  int q = (int)(t / OCR_DET_OUT_SCALE + 0.5f) + OCR_DET_OUT_ZP;

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

static uint32_t ocr_map_index(uint32_t x, uint32_t y)

{

  return y * OCR_DET_MAP_W + x;

}



static uint8_t ocr_map_vis_get(uint32_t x, uint32_t y)

{

  uint32_t i = ocr_map_index(x, y);

  return (uint8_t)((s_map_vis[i >> 3] >> (i & 7U)) & 1U);

}



static void ocr_map_vis_set(uint32_t x, uint32_t y)

{

  uint32_t i = ocr_map_index(x, y);

  s_map_vis[i >> 3] |= (uint8_t)(1U << (i & 7U));

}

/* 3×3 概率均值（QDQ 碎点平滑，再二值化；比 max 膨胀更接近 PC 连续热力图） */
static float ocr_prob_mean3x3(const int8_t *out_q, uint32_t cx, uint32_t cy)
{
  float sum = 0.0f;
  uint32_t cnt = 0U;
  int32_t y;
  int32_t x;

  for (y = (int32_t)cy - 1; y <= (int32_t)cy + 1; y++)
  {
    for (x = (int32_t)cx - 1; x <= (int32_t)cx + 1; x++)
    {
      if ((x < 0) || (y < 0) || (x >= (int32_t)OCR_DET_MAP_W) || (y >= (int32_t)OCR_DET_MAP_H))
      {
        continue;
      }
      sum += ocr_dequant_output(out_q[ocr_map_index((uint32_t)x, (uint32_t)y)]);
      cnt++;
    }
  }

  return (cnt > 0U) ? (sum / (float)cnt) : 0.0f;
}

static uint8_t ocr_prob_fg(const int8_t *out_q, uint32_t x, uint32_t y, float thresh)
{
  return (ocr_prob_mean3x3(out_q, x, y) >= thresh) ? 1U : 0U;
}

static uint16_t ocr_u8_to_rgb565(uint8_t r, uint8_t g, uint8_t b)
{
  return (uint16_t)(((uint16_t)(r & 0xF8U) << 8) | ((uint16_t)(g & 0xFCU) << 3) | ((uint16_t)b >> 3));
}

int ocr_infer_preprocess_rgb565(const uint16_t *rgb565, uint16_t src_w, uint16_t src_h)

{

  int8_t *in_q;

  uint32_t dy;

  uint32_t dx;



  if ((rgb565 == NULL) || (src_w == 0U) || (src_h == 0U))

  {

    return -1;

  }

  /* 拍照帧在 HyperRAM，CPU 读前必须 invalidate */
  SCB_InvalidateDCache_by_Addr((uint32_t *)(uintptr_t)rgb565,
                                 (int32_t)((uint32_t)src_w * (uint32_t)src_h * 2U));

  in_q = (int8_t *)(uintptr_t)OCR_DET_ACT_BASE;

  {
    /* NCHW int8 [1,3,160,160]：全帧双线性缩放 + ImageNet + QDQ（匹配量化脚本） */
    for (dy = 0U; dy < OCR_DET_PREPROC_H; dy++)
    {
      float fy = (((float)dy + 0.5f) * (float)src_h / (float)OCR_DET_PREPROC_H) - 0.5f;

      for (dx = 0U; dx < OCR_DET_PREPROC_W; dx++)
      {
        float fx = (((float)dx + 0.5f) * (float)src_w / (float)OCR_DET_PREPROC_W) - 0.5f;
        uint32_t plane = OCR_DET_PREPROC_W * OCR_DET_PREPROC_H;
        uint32_t idx = dy * OCR_DET_PREPROC_W + dx;
        float rf;
        float gf;
        float bf;
        uint8_t ru;
        uint8_t gu;
        uint8_t bu;

        rf = ocr_bilinear_f(rgb565, src_w, src_h, fx, fy, 0U);
        gf = ocr_bilinear_f(rgb565, src_w, src_h, fx, fy, 1U);
        bf = ocr_bilinear_f(rgb565, src_w, src_h, fx, fy, 2U);
        ru = ocr_f32_to_u8(rf);
        gu = ocr_f32_to_u8(gf);
        bu = ocr_f32_to_u8(bf);
        s_preproc_rgb565[idx] = ocr_u8_to_rgb565(ru, gu, bu);

#if OCR_INPUT_ORDER_BGR
        in_q[idx + 0U * plane] = ocr_quant_input(
            ocr_norm_channel(bu, OCR_NORM_MEAN_R, OCR_NORM_STD_R));

        in_q[idx + 1U * plane] = ocr_quant_input(
            ocr_norm_channel(gu, OCR_NORM_MEAN_G, OCR_NORM_STD_G));

        in_q[idx + 2U * plane] = ocr_quant_input(
            ocr_norm_channel(ru, OCR_NORM_MEAN_B, OCR_NORM_STD_B));
#else
        in_q[idx + 0U * plane] = ocr_quant_input(
            ocr_norm_channel(ru, OCR_NORM_MEAN_R, OCR_NORM_STD_R));

        in_q[idx + 1U * plane] = ocr_quant_input(
            ocr_norm_channel(gu, OCR_NORM_MEAN_G, OCR_NORM_STD_G));

        in_q[idx + 2U * plane] = ocr_quant_input(
            ocr_norm_channel(bu, OCR_NORM_MEAN_B, OCR_NORM_STD_B));
#endif
      }
    }
  }



  SCB_CleanDCache_by_Addr((uint32_t *)(uintptr_t)s_preproc_rgb565,
                          (int32_t)(sizeof(s_preproc_rgb565)));
  SCB_CleanDCache_by_Addr((uint32_t *)in_q, (int32_t)LL_ATON_OCR_DET_IN_1_SIZE_BYTES);

  {
    int32_t sum_q = 0;
    int8_t qmin = 127;
    int8_t qmax = -128;
    uint32_t n = OCR_DET_PREPROC_W * OCR_DET_PREPROC_H * 3U;
    uint32_t i;

    for (i = 0U; i < n; i++)
    {
      int8_t q = in_q[i];
      sum_q += (int32_t)q;
      if (q < qmin)
      {
        qmin = q;
      }
      if (q > qmax)
      {
        qmax = q;
      }
    }
    s_det_dbg.in_q_min = qmin;
    s_det_dbg.in_q_max = qmax;
    if (n > 0U)
    {
      s_det_dbg.in_q_mean_milli = (int16_t)((sum_q * 1000) / (int32_t)n);
    }
  }

  return 0;

}



#define OCR_INFER_TIMEOUT_MS  30000U
#define OCR_NPU_MAX_ITERS     250U
#define OCR_WFE_SLICE_MS      5U
/* 当前 LL_ATON_RT_DeInit_Network/RuntimeDeInit 会在 DONE 后卡住；先跳过以拿到完整报告 */
#define OCR_SKIP_ATON_DEINIT  1U

static void ocr_infer_runtime_teardown(void)
{
#if OCR_SKIP_ATON_DEINIT
  /* skip */
#else
  ocr_uart_phase("deinit net");
  LL_ATON_RT_DeInit_Network(&NN_Instance_ocr_det);
  ocr_uart_phase("deinit rt");
  LL_ATON_RT_RuntimeDeInit();
  ocr_uart_phase("deinit done");
#endif
}

static int ocr_infer_runtime_prepare(void)
{
  int wrc;

  OCR_DBG("RT init...");
  LL_ATON_RT_RuntimeInit();

  OCR_DBG("load wgt...");
  wrc = ocr_weights_load_to_hyperram();
  if (wrc == -2)
  {
    OCR_DBG("wgt not flashed");
    LL_ATON_RT_RuntimeDeInit();
    return -2;
  }
  if (wrc != 0)
  {
    char err_short[24];

    ocr_weights_get_last_error(err_short, sizeof(err_short));
    ocr_display_status_line2(err_short, RED);
    OCR_DBG("wgt load FAIL");
    LL_ATON_RT_RuntimeDeInit();
    return -1;
  }

  OCR_DBG("net init...");
  LL_ATON_RT_Init_Network(&NN_Instance_ocr_det);

  return 0;
}

static int ocr_infer_timed_out(uint32_t t0)
{
  return ((HAL_GetTick() - t0) > OCR_INFER_TIMEOUT_MS) ? 1 : 0;
}

/* ASYNC 时单次 __WFE() 会永久睡眠；分片等待以便外层超时生效 */
static void ocr_infer_wfe_slice(void)
{
  uint32_t slice_start = HAL_GetTick();

  while ((HAL_GetTick() - slice_start) < OCR_WFE_SLICE_MS)
  {
    LL_ATON_OSAL_WFE();
  }
}

static float ocr_map_compute_peak(const int8_t *out_q, int8_t *max_q_out)
{
  float peak = 0.0f;
  int8_t max_q = (int8_t)-128;

  for (uint32_t i = 0U; i < (OCR_DET_MAP_W * OCR_DET_MAP_H); i++)
  {
    float v = ocr_dequant_output(out_q[i]);

    if (v > peak)
    {
      peak = v;
      max_q = out_q[i];
    }
  }

  if (max_q_out != NULL)
  {
    *max_q_out = max_q;
  }

  return peak;
}

static const int8_t *ocr_infer_output_ptr(void)
{
  const LL_Buffer_InfoTypeDef *obi = LL_ATON_Output_Buffers_Info_ocr_det();

  if ((obi != NULL) && (obi[0].name != NULL))
  {
    return (const int8_t *)LL_Buffer_addr_start(&obi[0]);
  }

  return (const int8_t *)(uintptr_t)(OCR_DET_ACT_BASE + OCR_DET_OUT_OFFSET);
}

static void ocr_infer_snapshot_output(void)
{
  const int8_t *out_q = ocr_infer_output_ptr();
  int32_t sum_q = 0;
  int8_t qmin = 127;
  int8_t qmax = (int8_t)-128;
  uint32_t i;

  ocr_infer_fpu_enable();
  LL_ATON_Cache_MCU_Invalidate_Range((uintptr_t)out_q, OCR_DET_MAP_BYTES);
  memcpy(s_out_q_snap, out_q, OCR_DET_MAP_BYTES);

  for (i = 0U; i < OCR_DET_MAP_BYTES; i++)
  {
    int8_t q = s_out_q_snap[i];

    sum_q += (int32_t)q;
    if (q < qmin)
    {
      qmin = q;
    }
    if (q > qmax)
    {
      qmax = q;
    }
  }
  s_det_dbg.out_q_min = qmin;
  s_det_dbg.out_q_max = qmax;
  s_det_dbg.out_q_mean_milli = (int16_t)((sum_q * 1000) / (int32_t)OCR_DET_MAP_BYTES);
  s_last_peak_milli = (uint16_t)(ocr_map_compute_peak(s_out_q_snap, &s_last_max_q) * 1000.0f);
}

/* 真 Sigmoid 热力图稀疏；误读输入 R 平面时大量像素 >0.60 */
static uint8_t ocr_out_map_valid(const int8_t *out_q, float peak, uint32_t *hot_out)
{
  uint32_t hot = 0U;

  for (uint32_t i = 0U; i < (OCR_DET_MAP_W * OCR_DET_MAP_H); i++)
  {
    if (ocr_dequant_output(out_q[i]) > 0.60f)
    {
      hot++;
    }
  }

  if (hot_out != NULL)
  {
    *hot_out = hot;
  }

  /* QDQ 模型输出分布不同，放宽限制到 8000 */
  if (hot > 8000U)
  {
    return 0U;
  }

  /* 放宽 peak 检查 */
  if ((peak > 0.95f) && (hot > 2000U))
  {
    return 0U;
  }

  return 1U;
}

static void ocr_infer_show_npu_prog(uint32_t t0, uint32_t cur_step, int done)
{
  char prog[48];

  if (done != 0)
  {
    (void)snprintf(prog, sizeof(prog), "NPU DONE %lums", (unsigned long)(HAL_GetTick() - t0));
  }
  else if (cur_step >= OCR_DET_EPOCH_TOTAL)
  {
    (void)snprintf(prog, sizeof(prog), "NPU fin %lu %lums",
                   (unsigned long)cur_step, (unsigned long)(HAL_GetTick() - t0));
  }
  else
  {
    (void)snprintf(prog, sizeof(prog), "NPU %lu %lums",
                   (unsigned long)cur_step, (unsigned long)(HAL_GetTick() - t0));
  }
  OCR_DBG("%s", prog);
}

static int ocr_infer_exec_epochs(void)
{
  LL_ATON_RT_RetValues_t ret = LL_ATON_RT_DONE;
  uint32_t t0 = HAL_GetTick();
  uint32_t out_addr = OCR_DET_ACT_BASE + OCR_DET_OUT_OFFSET;
  uint32_t iter = 0U;
  char uart_buf[24];

  s_aton_blk_idx = 0U;
  s_last_snap_step = 0U;
  npu_cache_invalidate();
  SCB_InvalidateDCache_by_Addr((uint32_t *)(uintptr_t)out_addr, (int32_t)OCR_DET_MAP_BYTES);
  OCR_DBG("NPU run...");

  /*
   * POLLING 模式：RunEpochBlock 不应返回 WFE（见 ll_aton_runtime.c），勿调用 __WFE。
   * 只在 runtime 明确返回 LL_ATON_RT_DONE 后结束，不再按步数强制截断。
   */
  do
  {
    if (ocr_infer_timed_out(t0) != 0)
    {
      s_last_fail_blk = s_aton_blk_idx;
      ocr_uart_phase("npu TIMEOUT");
      OCR_DBG("NPU: TIMEOUT %lums blk=%lu", (unsigned long)(HAL_GetTick() - t0),
              (unsigned long)s_aton_blk_idx);
      return -1;
    }

    if (iter >= OCR_NPU_MAX_ITERS)
    {
      s_last_fail_blk = s_aton_blk_idx;
      ocr_uart_phase("npu ITERMAX");
      OCR_DBG("NPU: ITERMAX blk=%lu", (unsigned long)s_aton_blk_idx);
      return -1;
    }

    ret = LL_ATON_RT_RunEpochBlock(&NN_Instance_ocr_det);
    iter++;

    if ((iter % 50U) == 0U)
    {
      (void)snprintf(uart_buf, sizeof(uart_buf), "npu b%lu", (unsigned long)s_aton_blk_idx);
      ocr_uart_phase(uart_buf);
    }

    if (ret == LL_ATON_RT_WFE)
    {
      /* POLLING 下不应出现；直接重试，避免 __WFE 睡死 */
      OCR_DBG("NPU: unexpected WFE @%lu", (unsigned long)s_aton_blk_idx);
      continue;
    }

    if (ret == LL_ATON_RT_NO_WFE)
    {
      s_aton_blk_idx++;
      if ((s_aton_blk_idx <= 10U) || ((s_aton_blk_idx % 10U) == 0U) ||
          (s_aton_blk_idx >= OCR_DET_EPOCH_TOTAL))
      {
        ocr_infer_show_npu_prog(t0, s_aton_blk_idx, 0);
      }
    }

    if (ret == LL_ATON_RT_DONE)
    {
      if (s_aton_blk_idx == 0U)
      {
        s_aton_blk_idx = 1U;
      }
      break;
    }
  } while (1);

  ocr_infer_snapshot_output();
  s_last_snap_step = (uint16_t)((s_aton_blk_idx > 0U) ? s_aton_blk_idx : OCR_DET_RT_FINISH_STEPS);

  ocr_infer_show_npu_prog(t0, s_aton_blk_idx, 1);
  s_det_dbg.npu_ms = HAL_GetTick() - t0;
  OCR_DBG("snap q=%d pk=%u st=%u", (int)s_last_max_q, (unsigned)s_last_peak_milli,
          (unsigned)s_last_snap_step);
  OCR_DBG("NPU: DONE rt_steps=%lu ms=%lu", (unsigned long)s_aton_blk_idx,
          (unsigned long)(HAL_GetTick() - t0));
  return 0;
}

int ocr_infer_run_once(void)
{
  if (ocr_infer_runtime_prepare() != 0)
  {
    return -1;
  }

  if (ocr_infer_exec_epochs() != 0)
  {
    OCR_DBG("NPU: exec failed, deinit");
    ocr_infer_runtime_teardown();
    return -1;
  }

  OCR_DBG("NPU: DeInit_Network");
  ocr_infer_runtime_teardown();
  return 0;
}

int ocr_infer_capture_run(const uint16_t *rgb565, uint16_t src_w, uint16_t src_h,
                          ocr_det_result_t *result)
{
  int prep_rc;

  if (result == NULL)
  {
    return -1;
  }

  OCR_DBG("infer...");
  ocr_infer_prepare();

  prep_rc = ocr_infer_runtime_prepare();
  if (prep_rc == -2)
  {
    OCR_DBG("NPU FAIL: no wgt");
    (void)snprintf(result->result_text, sizeof(result->result_text),
                   "NPU\xE5\xA4\xB1\xE8\xB4\xA5:\xE6\x9C\xAA\xE7\x83\xA7\xE5\xBD\x95\xE6\x9D\x83\xE9\x87\x8D");
    return -2;
  }
  if (prep_rc != 0)
  {
    char err_short[24];

    ocr_weights_get_last_error(err_short, sizeof(err_short));
    OCR_DBG("NPU FAIL: wgt load");
    (void)snprintf(result->result_text, sizeof(result->result_text), "%s", err_short);
    return -2;
  }

  OCR_DBG("preproc...");
  if (ocr_infer_preprocess_rgb565(rgb565, src_w, src_h) != 0)
  {
    OCR_DBG("preproc FAIL");
    ocr_infer_runtime_teardown();
    (void)snprintf(result->result_text, sizeof(result->result_text),
                   "\xE9\xA2\x84\xE5\xA4\x84\xE7\x90\x86\xE5\xA4\xB1\xE8\xB4\xA5");
    return -1;
  }
  OCR_DBG("preproc OK");

  ocr_uart_phase("npu start");
  if (ocr_infer_exec_epochs() != 0)
  {
    ocr_uart_phase("npu TIMEOUT");
    OCR_DBG("NPU FAIL blk=%lu", (unsigned long)s_last_fail_blk);
    ocr_infer_runtime_teardown();
    (void)snprintf(result->result_text, sizeof(result->result_text),
                   "NPU\xE5\xA4\xB1\xE8\xB4\xA5:\xE8\xB6\x85\xE6\x97\xB6 blk%lu",
                   (unsigned long)s_last_fail_blk);
    return -2;
  }
  ocr_uart_phase("npu done");

  OCR_DBG("post...");
  ocr_uart_phase("post start");
  if (ocr_infer_postprocess(result, src_w, src_h) != 0)
  {
    ocr_uart_phase("post FAIL");
    OCR_DBG("post FAIL");
    ocr_infer_runtime_teardown();
    return -3;
  }
  ocr_uart_phase("post done");

  OCR_DBG("deinit...");
  ocr_infer_runtime_teardown();
  OCR_DBG("OK nb=%d", result->nb_box);
  /* 不在返回前再打 UART：HAL_UART_Transmit 可能在最后 TC 等待处阻塞。 */
  rgblcd_fill(0U, 0U, 24U, 24U, YELLOW);
  return 0;
}

int ocr_infer_session_begin(void)
{
  ocr_infer_prepare();
  return ocr_infer_runtime_prepare();
}

int ocr_infer_session_feed(const uint16_t *rgb565, uint16_t src_w, uint16_t src_h)
{
  return ocr_infer_preprocess_rgb565(rgb565, src_w, src_h);
}

int ocr_infer_session_run(void)
{
  return ocr_infer_exec_epochs();
}

int ocr_infer_session_post(ocr_det_result_t *result, uint16_t src_w, uint16_t src_h)
{
  return ocr_infer_postprocess(result, src_w, src_h);
}

void ocr_infer_session_end(void)
{
#if !OCR_SKIP_ATON_DEINIT
  ocr_infer_runtime_teardown();
#endif
}

/* 160×160 连通域 BFS → 轴对齐框；score 为连通域内概率均值 */
static uint32_t ocr_flood_box(const int8_t *heatmap, float thresh, int mx, int my, uint16_t *x1,
                              uint16_t *y1, uint16_t *x2, uint16_t *y2, float *prob_sum)
{
  uint32_t head = 0U;
  uint32_t tail = 0U;
  int min_x = mx;
  int max_x = mx;
  int min_y = my;
  int max_y = my;
  uint32_t area = 0U;
  /* 8 邻域，对齐 OpenCV findContours 默认连通性 */
  static const int dx[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
  static const int dy[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };

  uint32_t guard = 0U;

  s_flood_queue[tail++] = (uint16_t)ocr_map_index((uint32_t)mx, (uint32_t)my);
  ocr_map_vis_set((uint32_t)mx, (uint32_t)my);

  while (head < tail)
  {
    uint32_t idx;

    /* head 单调递增、最多 CAP 次，这里再加绝对上限做双保险 */
    if (++guard > (OCR_FLOOD_QUEUE_CAP + 16U))
    {
      break;
    }

    idx = s_flood_queue[head++];
    int cx = (int)(idx % OCR_DET_MAP_W);
    int cy = (int)(idx / OCR_DET_MAP_W);
    int k;

    area++;

    if (prob_sum != NULL)
    {
      *prob_sum += ocr_dequant_output(heatmap[idx]);
    }

    if (cx < min_x)
    {
      min_x = cx;
    }
    if (cx > max_x)
    {
      max_x = cx;
    }
    if (cy < min_y)
    {
      min_y = cy;
    }
    if (cy > max_y)
    {
      max_y = cy;
    }

    for (k = 0; k < 8; k++)
    {
      int nx = cx + dx[k];
      int ny = cy + dy[k];
      uint32_t nidx;

      if ((nx < 0) || (ny < 0) || (nx >= (int)OCR_DET_MAP_W) || (ny >= (int)OCR_DET_MAP_H))
      {
        continue;
      }

      nidx = ocr_map_index((uint32_t)nx, (uint32_t)ny);
      if (ocr_map_vis_get((uint32_t)nx, (uint32_t)ny) != 0U)
      {
        continue;
      }
      if (ocr_prob_fg(heatmap, (uint32_t)nx, (uint32_t)ny, thresh) == 0U)
      {
        continue;
      }
      if (tail >= OCR_FLOOD_QUEUE_CAP)
      {
        continue;
      }

      ocr_map_vis_set((uint32_t)nx, (uint32_t)ny);
      s_flood_queue[tail++] = (uint16_t)nidx;
    }
  }

  *x1 = (uint16_t)min_x;
  *y1 = (uint16_t)min_y;
  *x2 = (uint16_t)max_x;
  *y2 = (uint16_t)max_y;
  return area;
}



static void ocr_map_box_to_src(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2,
                               uint16_t src_w, uint16_t src_h, ocr_det_box_t *box);

static void ocr_unclip_map_box(uint16_t *x1, uint16_t *y1, uint16_t *x2, uint16_t *y2);

static void ocr_collect_boxes(ocr_det_result_t *result, const int8_t *out_q, float thresh,
                              uint16_t src_w, uint16_t src_h)
{
  uint32_t my;
  uint32_t mx;

  result->nb_box = 0;
  s_det_dbg.flood_cnt = 0U;
  s_det_dbg.rej_small = 0U;
  s_det_dbg.rej_score = 0U;
  s_det_dbg.max_area = 0U;
  s_rej_n = 0U;
  memset(s_det_dbg.box_score_milli, 0, sizeof(s_det_dbg.box_score_milli));
  memset(s_map_vis, 0, sizeof(s_map_vis));

  for (my = 0U; my < OCR_DET_MAP_H; my++)
  {
    for (mx = 0U; mx < OCR_DET_MAP_W; mx++)
    {
      uint16_t bx1;
      uint16_t by1;
      uint16_t bx2;
      uint16_t by2;
      uint32_t area;
      uint16_t span_w;
      uint16_t span_h;
      float score;
      ocr_det_box_t *box;

      float prob_sum = 0.0f;

      if (ocr_map_vis_get(mx, my) != 0U)
      {
        continue;
      }
      if (ocr_prob_fg(out_q, mx, my, thresh) == 0U)
      {
        continue;
      }
      if (result->nb_box >= (int)OCR_DET_MAX_BOXES)
      {
        return;
      }

      area = ocr_flood_box(out_q, thresh, (int)mx, (int)my, &bx1, &by1, &bx2, &by2, &prob_sum);
      s_det_dbg.flood_cnt++;
      if (area > (uint32_t)s_det_dbg.max_area)
      {
        s_det_dbg.max_area = (uint16_t)((area > 65535U) ? 65535U : area);
      }

      span_w = (uint16_t)(bx2 - bx1 + 1U);
      span_h = (uint16_t)(by2 - by1 + 1U);
      if ((span_w < OCR_DET_MAP_MIN_SPAN) || (span_h < OCR_DET_MAP_MIN_SPAN) ||
          (area < OCR_DET_MIN_AREA))
      {
        s_det_dbg.rej_small++;
        if (s_rej_n < OCR_DET_REJ_LOG)
        {
          s_rej_log[s_rej_n].area = (uint16_t)((area > 65535U) ? 65535U : area);
          s_rej_log[s_rej_n].w = span_w;
          s_rej_log[s_rej_n].h = span_h;
          s_rej_log[s_rej_n].score_milli = 0U;
          s_rej_log[s_rej_n].reason = 1U;
          s_rej_n++;
        }
        continue;
      }

      score = (area > 0U) ? (prob_sum / (float)area) : 0.0f;
      if (score < OCR_DB_BOX_THRESH)
      {
        s_det_dbg.rej_score++;
        if (s_rej_n < OCR_DET_REJ_LOG)
        {
          s_rej_log[s_rej_n].area = (uint16_t)((area > 65535U) ? 65535U : area);
          s_rej_log[s_rej_n].w = span_w;
          s_rej_log[s_rej_n].h = span_h;
          s_rej_log[s_rej_n].score_milli = (uint16_t)(score * 1000.0f);
          s_rej_log[s_rej_n].reason = 2U;
          s_rej_n++;
        }
        continue;
      }

      ocr_unclip_map_box(&bx1, &by1, &bx2, &by2);

      span_w = (uint16_t)(bx2 - bx1 + 1U);
      span_h = (uint16_t)(by2 - by1 + 1U);
      if ((span_w < (OCR_DET_MAP_MIN_SPAN + 2U)) || (span_h < (OCR_DET_MAP_MIN_SPAN + 2U)))
      {
        s_det_dbg.rej_small++;
        continue;
      }

      box = &result->boxes[result->nb_box];
      s_det_dbg.box_score_milli[result->nb_box] = (uint16_t)(score * 1000.0f);
      ocr_map_box_to_src(bx1, by1, bx2, by2, src_w, src_h, box);
      result->nb_box++;
    }
  }
}

static void ocr_unclip_map_box(uint16_t *x1, uint16_t *y1, uint16_t *x2, uint16_t *y2)
{
  uint16_t bw = (uint16_t)(*x2 - *x1 + 1U);
  uint16_t bh = (uint16_t)(*y2 - *y1 + 1U);
  float area = (float)bw * (float)bh;
  float perimeter = 2.0f * ((float)bw + (float)bh);
  uint16_t pad;

  if (perimeter <= 0.0f)
  {
    return;
  }

  pad = (uint16_t)((area * OCR_DB_UNCLIP_RATIO / perimeter) + 0.5f);
  if (pad == 0U)
  {
    pad = 1U;
  }

  if (*x1 >= pad)
  {
    *x1 = (uint16_t)(*x1 - pad);
  }
  else
  {
    *x1 = 0U;
  }
  if (*y1 >= pad)
  {
    *y1 = (uint16_t)(*y1 - pad);
  }
  else
  {
    *y1 = 0U;
  }

  *x2 = (uint16_t)(*x2 + pad);
  *y2 = (uint16_t)(*y2 + pad);
  if (*x2 >= OCR_DET_MAP_W)
  {
    *x2 = (uint16_t)(OCR_DET_MAP_W - 1U);
  }
  if (*y2 >= OCR_DET_MAP_H)
  {
    *y2 = (uint16_t)(OCR_DET_MAP_H - 1U);
  }
}

static uint16_t ocr_map_round(uint32_t map_c, uint16_t src_sz, uint16_t map_sz)
{
  int32_t v = (int32_t)(((int32_t)map_c * (int32_t)src_sz * 2 + (int32_t)map_sz) /
                        ((int32_t)map_sz * 2));

  if (v < 0)
  {
    v = 0;
  }
  if ((uint32_t)v >= (uint32_t)src_sz)
  {
    v = (int32_t)src_sz - 1;
  }
  return (uint16_t)v;
}



static void ocr_map_box_to_src(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2,
                               uint16_t src_w, uint16_t src_h, ocr_det_box_t *box)
{
  /* 对齐预处理：160×160 全帧映射回源图 */
  box->x1 = ocr_map_round((uint32_t)x1, src_w, OCR_DET_MAP_W);
  box->y1 = ocr_map_round((uint32_t)y1, src_h, OCR_DET_MAP_H);
  box->x2 = ocr_map_round((uint32_t)x2, src_w, OCR_DET_MAP_W);
  box->y2 = ocr_map_round((uint32_t)y2, src_h, OCR_DET_MAP_H);

  if (box->x1 > box->x2)
  {
    uint16_t t = box->x1;
    box->x1 = box->x2;
    box->x2 = t;
  }
  if (box->y1 > box->y2)
  {
    uint16_t t = box->y1;
    box->y1 = box->y2;
    box->y2 = t;
  }

  if (box->x2 >= src_w)
  {
    box->x2 = (uint16_t)(src_w - 1U);
  }
  if (box->y2 >= src_h)
  {
    box->y2 = (uint16_t)(src_h - 1U);
  }
}



int ocr_infer_postprocess(ocr_det_result_t *result, uint16_t src_w, uint16_t src_h)

{

  const int8_t *out_q;

  uint32_t my;

  uint32_t mx;

  float peak;

  OCR_DBG("PPenter");

  if (result == NULL)

  {

    return -1;

  }

  /* NPU 运行后 FPU CPACR 可能被清零，在此做防御性恢复 */
  ocr_infer_fpu_enable();

  OCR_DBG("PP a=%08lX n=%u", (unsigned long)(uintptr_t)result,
          (unsigned)sizeof(*result));

  {
    /* 用 volatile 字节清零取代 memset，规避 MVE/对齐路径，同时验证指针可写 */
    volatile uint8_t *p = (volatile uint8_t *)result;
    uint32_t n = (uint32_t)sizeof(*result);
    uint32_t i;

    for (i = 0U; i < n; i++)
    {
      p[i] = 0U;
    }
  }

  OCR_DBG("PP memset ok");

  out_q = s_out_q_snap;

  OCR_DBG("post p1 peak");

  peak = 0.0f;


  for (uint32_t i = 0U; i < (OCR_DET_MAP_W * OCR_DET_MAP_H); i++)

  {

    float v = ocr_dequant_output(out_q[i]);

    if (v > peak)

    {

      peak = v;

    }

  }

  OCR_DBG("post p2 valid");

  {
    uint32_t hot = 0U;
    float min_v = 1.0f, max_v = 0.0f, sum_v = 0.0f;

    if (ocr_out_map_valid(out_q, peak, &hot) == 0U)
    {
      int8_t in_min = s_det_dbg.in_q_min;
      int8_t in_max = s_det_dbg.in_q_max;
      int16_t in_mean = s_det_dbg.in_q_mean_milli;
      int8_t out_min = s_det_dbg.out_q_min;
      int8_t out_max = s_det_dbg.out_q_max;
      int16_t out_mean = s_det_dbg.out_q_mean_milli;
      uint32_t npu_ms = s_det_dbg.npu_ms;

      memset(&s_det_dbg, 0, sizeof(s_det_dbg));
      s_det_dbg.in_q_min = in_min;
      s_det_dbg.in_q_max = in_max;
      s_det_dbg.in_q_mean_milli = in_mean;
      s_det_dbg.out_q_min = out_min;
      s_det_dbg.out_q_max = out_max;
      s_det_dbg.out_q_mean_milli = out_mean;
      s_det_dbg.npu_ms = npu_ms;
      s_det_dbg.peak_milli = (uint16_t)(peak * 1000.0f);
      s_det_dbg.snap_step =
          (s_last_snap_step != 0U) ? s_last_snap_step : (uint16_t)OCR_DET_RT_FINISH_STEPS;
      s_det_dbg.hot_cnt = (uint16_t)((hot > 65535U) ? 65535U : hot);
      s_det_dbg.max_q = s_last_max_q;
      s_det_dbg.map_valid = 0U;
      s_rej_n = 0U;

      for (my = 0U; my < OCR_DET_HEAT_CELLS; my++)
      {
        for (mx = 0U; mx < OCR_DET_HEAT_CELLS; mx++)
        {
          float peak_cell = 0.0f;
          uint32_t yy;
          uint32_t xx;

          for (yy = 0U; yy < 4U; yy++)
          {
            for (xx = 0U; xx < 4U; xx++)
            {
              float v = ocr_dequant_output(
                  out_q[(my * 4U + yy) * OCR_DET_MAP_W + (mx * 4U + xx)]);

              if (v > peak_cell)
              {
                peak_cell = v;
              }
            }
          }
          s_proc_heat[my * OCR_DET_HEAT_CELLS + mx] = peak_cell;
        }
      }

      (void)snprintf(result->result_text, sizeof(result->result_text),
                     "BAD MAP hot=%lu pk=%u", (unsigned long)hot,
                     (unsigned)(uint16_t)(peak * 1000.0f));
      OCR_DBG("%s", result->result_text);
      return 0;
    }

    s_det_dbg.hot_cnt = (uint16_t)((hot > 65535U) ? 65535U : hot);
    s_det_dbg.map_valid = 1U;

    /* 计算热力图统计信息 */
    for (uint32_t i = 0U; i < (OCR_DET_MAP_W * OCR_DET_MAP_H); i++)
    {
      float v = ocr_dequant_output(out_q[i]);
      if (v < min_v) min_v = v;
      if (v > max_v) max_v = v;
      sum_v += v;
    }
    s_det_dbg.heat_min_milli = (uint16_t)(min_v * 1000.0f);
    s_det_dbg.heat_mean_milli =
        (uint16_t)((sum_v / (float)(OCR_DET_MAP_W * OCR_DET_MAP_H)) * 1000.0f);
    OCR_DBG("heatmap min=%d max=%d mean=%d hot60=%lu",
            (int)(min_v * 1000), (int)(max_v * 1000),
            (int)((sum_v / (OCR_DET_MAP_W * OCR_DET_MAP_H)) * 1000),
            (unsigned long)hot);
  }



  /* 与 Python DBPostProcess.thresh 一致 */
  s_heat_thresh = OCR_DB_THRESH;

  s_last_peak_milli = (uint16_t)(peak * 1000.0f);
  s_last_thresh_milli = (uint16_t)(s_heat_thresh * 1000.0f);
  s_det_dbg.peak_milli = s_last_peak_milli;
  s_det_dbg.thresh_milli = s_last_thresh_milli;
  s_det_dbg.box_thresh_milli = (uint16_t)(OCR_DB_BOX_THRESH * 1000.0f);
  s_det_dbg.snap_step = s_last_snap_step;

  OCR_DBG("post p3 box");

  ocr_collect_boxes(result, out_q, s_heat_thresh, src_w, src_h);

  OCR_DBG("post p4 heat");

  for (my = 0U; my < OCR_DET_HEAT_CELLS; my++)
  {
    for (mx = 0U; mx < OCR_DET_HEAT_CELLS; mx++)
    {
      float peak_cell = 0.0f;
      uint32_t dy;
      uint32_t dx;

      for (dy = 0U; dy < 4U; dy++)
      {
        for (dx = 0U; dx < 4U; dx++)
        {
          float v = ocr_dequant_output(out_q[(my * 4U + dy) * OCR_DET_MAP_W + (mx * 4U + dx)]);

          if (v > peak_cell)
          {
            peak_cell = v;
          }
        }
      }
      s_proc_heat[my * OCR_DET_HEAT_CELLS + mx] = peak_cell;
    }
  }

  /* 尚无 rec 模型：状态栏显示检测框数量；换 rec 后可在此填识别文本 */
  /* 简化调试：直接显示 nb, peak, threshold, max_q, above_count */
  {
    uint16_t pk = (uint16_t)(peak * 1000.0f);
    uint16_t th = (uint16_t)(s_heat_thresh * 1000.0f);
    uint32_t above_th = 0U;
    int8_t max_q_val = -128;
    uint32_t gy;
    uint32_t gx;

    for (gy = 0U; gy < OCR_DET_MAP_H; gy++)
    {
      for (gx = 0U; gx < OCR_DET_MAP_W; gx++)
      {
        uint32_t i = ocr_map_index(gx, gy);

        if (ocr_prob_fg(out_q, gx, gy, s_heat_thresh) != 0U)
        {
          above_th++;
        }
        if (out_q[i] > max_q_val)
        {
          max_q_val = out_q[i];
        }
      }
    }
    s_det_dbg.above_cnt = (uint16_t)above_th;
    s_det_dbg.max_q = max_q_val;
    s_det_dbg.nb_box = result->nb_box;
    (void)snprintf(result->result_text, sizeof(result->result_text),
                   "nb=%d pk%u th%u ab%u",
                   result->nb_box, (unsigned)pk, (unsigned)th, (unsigned)above_th);
    OCR_DBG("%s", result->result_text);
  }

  return 0;

}



const float *ocr_infer_get_heatmap(float *out_thresh)
{
  if (out_thresh != NULL)
  {
    *out_thresh = s_heat_thresh;
  }
  return s_proc_heat;
}

const ocr_det_dbg_t *ocr_infer_get_dbg_stats(void)
{
  return &s_det_dbg;
}

void ocr_infer_get_rej_log(const ocr_det_rej_t **out, uint8_t *out_n)
{
  if (out != NULL)
  {
    *out = s_rej_log;
  }
  if (out_n != NULL)
  {
    *out_n = s_rej_n;
  }
}

const uint16_t *ocr_infer_get_preproc_rgb565(uint16_t *out_w, uint16_t *out_h)
{
  if (out_w != NULL)
  {
    *out_w = OCR_DET_PREPROC_W;
  }
  if (out_h != NULL)
  {
    *out_h = OCR_DET_PREPROC_H;
  }
  return s_preproc_rgb565;
}

