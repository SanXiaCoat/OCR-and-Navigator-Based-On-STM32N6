#include "ocr_display.h"
#include "./RGBLCD/rgblcd.h"
#include "./OV5640/ov5640.h"
#include "text.h"
#include <stdio.h>
#include <string.h>

#define OCR_STATUS_FONT_SMALL  12U
#define OCR_STATUS_LINE1_Y     4U
#define OCR_STATUS_LINE2_Y     28U

static char s_status_line1[80];
static char s_status_line2[80];
static uint16_t s_status_line1_color = BLUE;
static uint16_t s_status_line2_color = RED;

static ocr_focus_dbg_t s_focus_dbg = { 0xFFU, 0xFFU, 0xFFU, 0U, 0U, 0U };

static void ocr_display_draw_bar(void)
{
  uint16_t w = (uint16_t)(rgblcddev.width - 1U);

  rgblcd_fill(0U, 0U, w, (uint16_t)(OCR_STATUS_BAR_H - 1U), LGRAY);
  rgblcd_fill(0U, (uint16_t)(OCR_STATUS_BAR_H - 1U), w, (uint16_t)(OCR_STATUS_BAR_H - 1U), GRAY);

  if (s_status_line1[0] != '\0')
  {
    text_show_string(4U, OCR_STATUS_LINE1_Y, (uint16_t)(rgblcddev.width - 8U), 16U,
                     s_status_line1, OCR_STATUS_FONT_SMALL, 0U, 0U, s_status_line1_color);
  }

  if (s_status_line2[0] != '\0')
  {
    text_show_string(4U, OCR_STATUS_LINE2_Y, (uint16_t)(rgblcddev.width - 8U), 16U,
                     s_status_line2, OCR_STATUS_FONT_SMALL, 0U, 0U, s_status_line2_color);
  }
}

uint16_t ocr_display_bar_height(void)
{
  return OCR_STATUS_BAR_H;
}

void ocr_display_status_redraw(void)
{
  ocr_display_draw_bar();
}

void ocr_display_status_show2(const char *line1, const char *line2, uint16_t color)
{
  s_status_line1[0] = '\0';
  s_status_line2[0] = '\0';

  if (line1 != NULL)
  {
    (void)strncpy(s_status_line1, line1, sizeof(s_status_line1) - 1U);
    s_status_line1[sizeof(s_status_line1) - 1U] = '\0';
    s_status_line1_color = color;
  }

  if (line2 != NULL)
  {
    (void)strncpy(s_status_line2, line2, sizeof(s_status_line2) - 1U);
    s_status_line2[sizeof(s_status_line2) - 1U] = '\0';
    s_status_line2_color = color;
  }

  ocr_display_draw_bar();
}

void ocr_display_status_line1(const char *utf8_text, uint16_t color)
{
  if (utf8_text == NULL)
  {
    return;
  }

  (void)strncpy(s_status_line1, utf8_text, sizeof(s_status_line1) - 1U);
  s_status_line1[sizeof(s_status_line1) - 1U] = '\0';
  s_status_line1_color = color;
  ocr_display_draw_bar();
}

void ocr_display_status_line2(const char *utf8_text, uint16_t color)
{
  if (utf8_text == NULL)
  {
    return;
  }

  (void)strncpy(s_status_line2, utf8_text, sizeof(s_status_line2) - 1U);
  s_status_line2[sizeof(s_status_line2) - 1U] = '\0';
  s_status_line2_color = color;
  ocr_display_draw_bar();
}

void ocr_display_status_show(const char *utf8_text, uint16_t color)
{
  ocr_display_status_show2(utf8_text, NULL, color);
}

void ocr_display_error_show(const char *ascii_text)
{
  ocr_display_status_line2(ascii_text, RED);
}

void ocr_display_error_clear(void)
{
  s_status_line2[0] = '\0';
  ocr_display_draw_bar();
}

void ocr_display_show(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                      const char *utf8_text, uint8_t font_size, uint16_t color)
{
  if (utf8_text == NULL)
  {
    return;
  }

  text_show_string(x, y, width, height, (char *)utf8_text, font_size, 1U, 0U, color);
}

#define OCR_DBG_PANEL_W   112U
#define OCR_REC_PANEL_W   200U
#define OCR_DBG_FONT      12U
#define OCR_DBG_LINE_H    14U
#define OCR_AF_LINES_H    42U

static void ocr_display_blit_rgb565_thumb(uint16_t dst_x, uint16_t dst_y, uint16_t w, uint16_t h,
                                          const uint16_t *src)
{
  if (src == NULL)
  {
    return;
  }

  SCB_CleanDCache_by_Addr((uint32_t *)(uintptr_t)src, (int32_t)((uint32_t)w * (uint32_t)h * 2U));
  rgblcd_color_fill(dst_x, dst_y, (uint16_t)(dst_x + w - 1U), (uint16_t)(dst_y + h - 1U),
                    (uint16_t *)(uintptr_t)src);
}

void ocr_display_debug_det_right(const ocr_det_dbg_t *dbg)
{
  char line[20];
  uint16_t x0;
  uint16_t y0;
  uint16_t y;
  uint8_t row;

  if (dbg == NULL)
  {
    return;
  }

  if (rgblcddev.width <= OCR_DBG_PANEL_W)
  {
    return;
  }

  x0 = (uint16_t)(rgblcddev.width - OCR_DBG_PANEL_W);
  y0 = (uint16_t)(OCR_STATUS_BAR_H + 4U);
  rgblcd_fill(x0, y0, (uint16_t)(rgblcddev.width - 1U),
              (uint16_t)(y0 + (12U * OCR_DBG_LINE_H) - 1U), LGRAY);

  y = y0;
  for (row = 0U; row < 12U; row++)
  {
    line[0] = '\0';
    switch (row)
    {
      case 0U:
        (void)snprintf(line, sizeof(line), "DET DBG");
        break;
      case 1U:
        (void)snprintf(line, sizeof(line), "pk %u", (unsigned)dbg->peak_milli);
        break;
      case 2U:
        (void)snprintf(line, sizeof(line), "th %u", (unsigned)dbg->thresh_milli);
        break;
      case 3U:
        (void)snprintf(line, sizeof(line), "bt %u", (unsigned)dbg->box_thresh_milli);
        break;
      case 4U:
        (void)snprintf(line, sizeof(line), "ab %u", (unsigned)dbg->above_cnt);
        break;
      case 5U:
        (void)snprintf(line, sizeof(line), "fl %u", (unsigned)dbg->flood_cnt);
        break;
      case 6U:
        (void)snprintf(line, sizeof(line), "rj %u/%u",
                       (unsigned)dbg->rej_small, (unsigned)dbg->rej_score);
        break;
      case 7U:
        (void)snprintf(line, sizeof(line), "ma %u nb%d",
                       (unsigned)dbg->max_area, dbg->nb_box);
        break;
      case 8U:
        (void)snprintf(line, sizeof(line), "st %u mx%d",
                       (unsigned)dbg->snap_step, (int)dbg->max_q);
        break;
      case 9U:
      case 10U:
      case 11U:
      {
        const ocr_focus_dbg_t *af = ocr_display_focus_get();

        if (af == NULL)
        {
          break;
        }
        if (row == 9U)
        {
          (void)snprintf(line, sizeof(line), "AF i%u c%u s%u",
                         (unsigned)af->init_rc, (unsigned)af->const_rc, (unsigned)af->single_rc);
        }
        else if (row == 10U)
        {
          (void)snprintf(line, sizeof(line), "R29:%02X R23:%02X",
                         (unsigned)af->st3029, (unsigned)af->st3023);
        }
        else
        {
          if (af->st3029 == 0x10U)
          {
            (void)snprintf(line, sizeof(line), "AF OK");
          }
          else if (af->single_rc == 1U)
          {
            (void)snprintf(line, sizeof(line), "AF TIMEOUT");
          }
          else if (af->st3029 == 0x70U)
          {
            (void)snprintf(line, sizeof(line), "AF IDLE");
          }
          else
          {
            (void)snprintf(line, sizeof(line), "AF %02X", (unsigned)af->st3029);
          }
        }
        break;
      }
      default:
        break;
    }

    if (line[0] != '\0')
    {
      uint16_t color = BLUE;

      if (row >= 9U)
      {
        const ocr_focus_dbg_t *af = ocr_display_focus_get();

        color = (af != NULL && af->single_rc == 0U) ? GREEN : RED;
        if (row == 10U)
        {
          color = BLUE;
        }
      }
      text_show_string((uint16_t)(x0 + 2U), y, (uint16_t)(OCR_DBG_PANEL_W - 4U), OCR_DBG_LINE_H,
                       line, OCR_DBG_FONT, 1U, 0U, color);
    }
    y = (uint16_t)(y + OCR_DBG_LINE_H);
  }
}

void ocr_display_focus_set_init(uint8_t rc)
{
  s_focus_dbg.init_rc = rc;
}

void ocr_display_focus_set_const(uint8_t rc)
{
  s_focus_dbg.const_rc = rc;
}

void ocr_display_focus_set_single(uint8_t rc)
{
  s_focus_dbg.single_rc = rc;
}

void ocr_display_focus_poll(void)
{
  (void)ov5640_focus_read_status(&s_focus_dbg.st3029, &s_focus_dbg.st3023, &s_focus_dbg.cmd3022);
}

const ocr_focus_dbg_t *ocr_display_focus_get(void)
{
  return &s_focus_dbg;
}

#define OCR_PREPROC_THUMB_W  OCR_DET_PREPROC_W
#define OCR_PREPROC_THUMB_H  OCR_DET_PREPROC_H
#define OCR_PREPROC_MARGIN   4U

void ocr_display_show_preproc_left(uint16_t img_x, uint16_t img_y, uint16_t img_w, uint16_t img_h)
{
  uint16_t thumb_x;
  uint16_t thumb_y;
  uint16_t af_y;
  uint16_t thumb_w;
  uint16_t thumb_h;
  const uint16_t *rgb;
  char line[28];
  const ocr_focus_dbg_t *af;

  (void)img_w;
  thumb_w = OCR_PREPROC_THUMB_W;
  thumb_h = OCR_PREPROC_THUMB_H;
  thumb_x = (uint16_t)(img_x + OCR_PREPROC_MARGIN);
  /* 缩略图上方预留 AF 文字区，避免画到屏幕外 */
  thumb_y = (uint16_t)(img_y + img_h - thumb_h - OCR_PREPROC_MARGIN - OCR_AF_LINES_H);
  if (thumb_y < (uint16_t)(img_y + OCR_AF_LINES_H))
  {
    thumb_y = (uint16_t)(img_y + OCR_AF_LINES_H);
  }
  af_y = (uint16_t)(thumb_y - OCR_AF_LINES_H);

  rgblcd_fill(thumb_x, af_y,
              (uint16_t)(thumb_x + thumb_w + 4U),
              (uint16_t)(thumb_y - 1U), LGRAY);

  af = ocr_display_focus_get();
  if (af != NULL)
  {
    (void)snprintf(line, sizeof(line), "AF i%u c%u s%u",
                   (unsigned)af->init_rc, (unsigned)af->const_rc, (unsigned)af->single_rc);
    text_show_string(thumb_x, af_y, (uint16_t)(thumb_w + 4U), 12U,
                     line, OCR_STATUS_FONT_SMALL, 1U, 0U,
                     (af->single_rc == 0U) ? GREEN : RED);
    (void)snprintf(line, sizeof(line), "R29:%02X R23:%02X",
                   (unsigned)af->st3029, (unsigned)af->st3023);
    text_show_string(thumb_x, (uint16_t)(af_y + 14U), (uint16_t)(thumb_w + 4U), 12U,
                     line, OCR_STATUS_FONT_SMALL, 1U, 0U, BLUE);
    if (af->st3029 == 0x10U)
    {
      (void)snprintf(line, sizeof(line), "AF OK");
    }
    else if (af->single_rc == 1U)
    {
      (void)snprintf(line, sizeof(line), "AF TIMEOUT");
    }
    else
    {
      (void)snprintf(line, sizeof(line), "AF st %02X", (unsigned)af->st3029);
    }
    text_show_string(thumb_x, (uint16_t)(af_y + 28U), (uint16_t)(thumb_w + 4U), 12U,
                     line, OCR_STATUS_FONT_SMALL, 1U, 0U, YELLOW);
  }

  rgb = ocr_infer_get_preproc_rgb565(NULL, NULL);
  if (rgb != NULL)
  {
    ocr_display_blit_rgb565_thumb(thumb_x, thumb_y, thumb_w, thumb_h, rgb);
    rgblcd_draw_rectangle((uint16_t)(thumb_x - 1U), (uint16_t)(thumb_y - 1U),
                          (uint16_t)(thumb_x + thumb_w),
                          (uint16_t)(thumb_y + thumb_h), RED);
    text_show_string(thumb_x, (uint16_t)(thumb_y + thumb_h + 2U), thumb_w, 12U,
                     "det bilinear", OCR_STATUS_FONT_SMALL, 1U, 0U, RED);
  }
}

void ocr_display_rec_text_right(const ocr_det_result_t *result)
{
  uint16_t x0;
  uint16_t y0;
  uint16_t y;
  int i;

  if (result == NULL)
  {
    return;
  }
  if (rgblcddev.width <= (OCR_DBG_PANEL_W + OCR_REC_PANEL_W + 8U))
  {
    return;
  }

  x0 = (uint16_t)(rgblcddev.width - OCR_DBG_PANEL_W - OCR_REC_PANEL_W - 4U);
  y0 = (uint16_t)(OCR_STATUS_BAR_H + 4U);
  rgblcd_fill(x0, y0,
              (uint16_t)(x0 + OCR_REC_PANEL_W - 1U),
              (uint16_t)(y0 + (uint16_t)(OCR_DET_MAX_BOXES * 18U) + 16U),
              WHITE);

  text_show_string((uint16_t)(x0 + 2U), y0, (uint16_t)(OCR_REC_PANEL_W - 4U), 14U,
                   "REC TEXT", OCR_DBG_FONT, 1U, 0U, BLUE);
  y = (uint16_t)(y0 + 16U);

  if (result->nb_box <= 0)
  {
    text_show_string((uint16_t)(x0 + 2U), y, (uint16_t)(OCR_REC_PANEL_W - 4U), 14U,
                     "(no boxes)", OCR_DBG_FONT, 1U, 0U, GRAY);
    return;
  }

  for (i = 0; i < result->nb_box; i++)
  {
    char line[72];

    if (result->rec_text[i][0] != '\0')
    {
      (void)snprintf(line, sizeof(line), "%d:%s", i + 1, result->rec_text[i]);
    }
    else
    {
      (void)snprintf(line, sizeof(line), "%d:#%d", i + 1, i + 1);
    }
    text_show_string((uint16_t)(x0 + 2U), y, (uint16_t)(OCR_REC_PANEL_W - 4U), 16U,
                     line, OCR_DBG_FONT, 1U, 0U, BLACK);
    y = (uint16_t)(y + 18U);
    if (y > (uint16_t)(rgblcddev.height - 20U))
    {
      break;
    }
  }
}
