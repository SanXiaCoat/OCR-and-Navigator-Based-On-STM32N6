#ifndef __OCR_INFER_H
#define __OCR_INFER_H

#include <stdint.h>

/* 与 ExtMemLoader 生成配置一致（ch_PPOCRv4_det_160_qdq；320 需重新 Generate 后改下列宏） */
#define OCR_DET_WEIGHTS_XSPI2_BASE   0x71000000U
/* ocr_det_generate_report: 0x71000000–0x7115A050，32B 对齐供 DCache invalidate */
#define OCR_DET_WEIGHTS_BYTES        0x15A080U
#define OCR_DET_MAP_W                160U
#define OCR_DET_MAP_H                160U
#define OCR_DET_MAP_BYTES            (OCR_DET_MAP_W * OCR_DET_MAP_H)
#define OCR_DET_HEAT_CELLS           (OCR_DET_MAP_W / 4U)
#define OCR_DET_OUT_OFFSET           0U
/* scheduling epoch 数（generate report）；运行结束以 runtime 返回 DONE 为准 */
#define OCR_DET_EPOCH_TOTAL          184U
#define OCR_DET_RT_FINISH_STEPS      184U
/* QDQ 输入/输出 scale & zero_point（generate report） */
#define OCR_DET_IN_SCALE             0.009904302656650543f
#define OCR_DET_IN_ZP                (-104)
#define OCR_DET_OUT_SCALE            0.003921568859368563f
#define OCR_DET_OUT_ZP               (-128)
#define OCR_DET_MAX_BOXES            8U
#define OCR_DET_PREPROC_W            160U
#define OCR_DET_PREPROC_H            160U
#define OCR_DET_PREPROC_PIX          (OCR_DET_PREPROC_W * OCR_DET_PREPROC_H)
/* DB 后处理在 160×160 概率图上做连通域（与 PP-OCR det 输入尺寸一致） */
#define OCR_DET_MAP_MIN_SPAN         3U
#define OCR_DET_MIN_AREA             20U  /* 最小连通域像素数 */
/* unclip 在 ocr_infer.c 内按 Python area*ratio/perimeter 计算，不再用百分比扩展 */

typedef struct
{
  uint16_t x1;
  uint16_t y1;
  uint16_t x2;
  uint16_t y2;
} ocr_det_box_t;

typedef struct
{
  int nb_box;
  ocr_det_box_t boxes[OCR_DET_MAX_BOXES];
  char result_text[160]; /* UTF-8，供 ocr_display 显示 */
  char rec_text[OCR_DET_MAX_BOXES][64]; /* 每框识别英文 */
  float rec_score[OCR_DET_MAX_BOXES];
} ocr_det_result_t;

/** 最近一次 det 后处理调试量（整数毫单位，避免 nano 无 %f） */
typedef struct
{
  uint16_t peak_milli;
  uint16_t thresh_milli;
  uint16_t box_thresh_milli;
  uint16_t above_cnt;
  uint16_t flood_cnt;
  uint16_t rej_small;
  uint16_t rej_score;
  uint16_t max_area;
  int8_t max_q;
  uint16_t snap_step;
  int nb_box;
  uint16_t hot_cnt;          /* 热力图 >0.60 像素数 */
  uint16_t heat_min_milli;
  uint16_t heat_mean_milli;
  int8_t in_q_min;
  int8_t in_q_max;
  int16_t in_q_mean_milli;   /* 输入 int8 均值×1000 */
  int8_t out_q_min;
  int8_t out_q_max;
  int16_t out_q_mean_milli;  /* 输出 int8 均值×1000 */
  uint32_t npu_ms;
  uint8_t map_valid;         /* 1=通过 BAD MAP 检查 */
  uint16_t box_score_milli[OCR_DET_MAX_BOXES];
} ocr_det_dbg_t;

#define OCR_DET_REJ_LOG  4U

typedef struct
{
  uint16_t area;
  uint16_t w;
  uint16_t h;
  uint16_t score_milli;
  uint8_t reason; /* 1=small 2=score */
} ocr_det_rej_t;

void ocr_infer_init(void);

/** @return 1=OK，0=空白(0xFF)，-1=读失败；可选输出首 word 与 read_cpu 错误码 */
int ocr_infer_weights_probe(uint32_t *w0, uint8_t *rd_out);

/** @brief 检查 NOR @0x71000000 权重是否已烧录；0=空/未烧，1=OK，-1=读失败 */
int ocr_infer_weights_ok(void);

/** @brief 首次 OCR 前调用：NPU 时钟/权重检查（仅执行一次） */
int ocr_infer_prepare(void);

/**
 * @brief RGB565 帧预处理并写入 ATON 输入区
 */
int ocr_infer_preprocess_rgb565(const uint16_t *rgb565, uint16_t src_w, uint16_t src_h);

/** @brief 执行一次推理（须先 preprocess） */
int ocr_infer_run_once(void);

/**
 * @brief 解析 det 热力图，将框映射到 src_w x src_h
 */
int ocr_infer_postprocess(ocr_det_result_t *result, uint16_t src_w, uint16_t src_h);

/** @brief 预处理 + 推理 + 后处理 */
int ocr_infer_capture_run(const uint16_t *rgb565, uint16_t src_w, uint16_t src_h,
                          ocr_det_result_t *result);

/* 拆分版会话接口：用于绕开 ocr_infer_capture_run() 的可疑返回路径 */
int ocr_infer_session_begin(void);
int ocr_infer_session_feed(const uint16_t *rgb565, uint16_t src_w, uint16_t src_h);
int ocr_infer_session_run(void);
int ocr_infer_session_post(ocr_det_result_t *result, uint16_t src_w, uint16_t src_h);
void ocr_infer_session_end(void);

/** 最近一次后处理记录的拒绝样本（连通域过小 / box_score 不足） */
void ocr_infer_get_rej_log(const ocr_det_rej_t **out, uint8_t *out_n);

/**
 * @brief 最近一次后处理的热力图指针与自适应阈值（供叠图显示）
 * @param out_thresh 非 NULL 时返回本次使用的二值化阈值
 */
const float *ocr_infer_get_heatmap(float *out_thresh);

/** 最近一次后处理统计，供右侧调试面板 */
const ocr_det_dbg_t *ocr_infer_get_dbg_stats(void);

/** 最近一次 preprocess 的 160×160 RGB565（量化前缩放图） */
const uint16_t *ocr_infer_get_preproc_rgb565(uint16_t *out_w, uint16_t *out_h);

#endif /* __OCR_INFER_H */
