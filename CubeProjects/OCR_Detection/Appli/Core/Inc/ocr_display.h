#ifndef __OCR_DISPLAY_H
#define __OCR_DISPLAY_H

#include "main.h"
#include "ocr_infer.h"

/** 顶部状态栏高度；预览区从该 Y 起，所有 OCR 提示只画在此区域内 */
#define OCR_STATUS_BAR_H  56U

uint16_t ocr_display_bar_height(void);

/** 在顶部状态栏显示 UTF-8 文字（清栏 + 单行，兼容旧调用） */
void ocr_display_status_show(const char *utf8_text, uint16_t color);

/** 双行状态栏：line1(y=4) 进度 + line2(y=28) 错误/提示，line2 可为 NULL */
void ocr_display_status_show2(const char *line1, const char *line2, uint16_t color);

/** 仅更新第 1 行（保留第 2 行），供 OCR_DBG 进度用 */
void ocr_display_status_line1(const char *utf8_text, uint16_t color);

/** 仅更新第 2 行（保留第 1 行），供错误码 ASCII 用 */
void ocr_display_status_line2(const char *utf8_text, uint16_t color);

/** 在状态栏第 2 行显示 ASCII 错误（不画在预览区） */
void ocr_display_error_show(const char *ascii_text);

void ocr_display_error_clear(void);

/** 预览帧刷新后重绘状态栏，防止被画面盖住 */
void ocr_display_status_redraw(void);

/** 任意位置显示（叠加 mode=1，仍可能被图像挡住，仅保留兼容） */
void ocr_display_show(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                      const char *utf8_text, uint8_t font_size, uint16_t color);

/** 预览图右侧窄条显示 det 调试参数（不遮挡主画面中心） */
void ocr_display_debug_det_right(const ocr_det_dbg_t *dbg);

/** OV5640 对焦结果记录（0=OK 1=FAIL 0xFF=未调用） */
typedef struct
{
  uint8_t init_rc;
  uint8_t const_rc;
  uint8_t single_rc;
  uint8_t st3029;
  uint8_t st3023;
  uint8_t cmd3022;
} ocr_focus_dbg_t;

void ocr_display_focus_set_init(uint8_t rc);
void ocr_display_focus_set_const(uint8_t rc);
void ocr_display_focus_set_single(uint8_t rc);
void ocr_display_focus_poll(void);
const ocr_focus_dbg_t *ocr_display_focus_get(void);

/** 左下角 160×160 模型输入预览 + 对焦状态 */
void ocr_display_show_preproc_left(uint16_t img_x, uint16_t img_y, uint16_t img_w, uint16_t img_h);

/** 右侧识别文本列表（det+rec 级联后） */
void ocr_display_rec_text_right(const ocr_det_result_t *result);

#endif
