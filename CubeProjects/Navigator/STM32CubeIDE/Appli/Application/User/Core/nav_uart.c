/**
 * @file nav_uart.c
 * @brief UART4 接收 navi_update JSON（curRoad/action/actionText/nextActionDistance/nextRoad/remainTime）
 */

#include "nav_uart.h"
#include "nav_glyphs_builtin.h"
#include "uart_port.h"
#include "./RGBLCD/rgblcd.h"
#include "ocr_display.h"
#include "text.h"
#include "./SD_NAND/sd_nand.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define NAV_LINE_MAX          768U
#define NAV_FIELD_MAX         160U
#define NAV_RX_TIMEOUT_MS     0U
#define NAV_LINK_LOST_MS      30000U
#define NAV_RX_BUDGET_BYTES   256U
#define NAV_GLYPH_CACHE_COUNT 128U
#define NAV_GLYPH16_BYTES     32U
#define NAV_ROW_COUNT         5U
#define NAV_ICON_BOX          52U
#define NAV_ICON_PAD          8U
#define NAV_TEXT_X            (NAV_ICON_PAD + NAV_ICON_BOX + 8U)
#define NAV_FONT_SIZE         16U

static const char NAV_LBL_CUR_ZH[]     = "\xE5\xBD\x93\xE5\x89\x8D\xE4\xBD\x8D\xE7\xBD\xAE\xEF\xBC\x9A";
static const char NAV_LBL_NEXT_ZH[]    = "\xE5\x8D\xB3\xE5\xB0\x86\xE8\xBF\x9B\xE5\x85\xA5\xEF\xBC\x9A";
static const char NAV_LBL_STEP_ZH[]    = "\xE5\x88\xB0\xE4\xB8\x8B\xE4\xB8\x80\xE6\xAD\xA5\xE7\x9A\x84\xE8\xB7\x9D\xE7\xA6\xBB\xEF\xBC\x9A";
static const char NAV_LBL_ETA_ZH[]     = "\xE5\x89\xA9\xE4\xBD\x99\xE6\x97\xB6\xE9\x97\xB4\xEF\xBC\x9A";
static const char NAV_TITLE[]          = "Navigation";
static const char NAV_WAIT[]           = "Waiting for nav data...";
static const char NAV_LOST[]           = "Navigation lost";

typedef enum
{
  NAV_ICON_UNKNOWN = 0,
  NAV_ICON_STRAIGHT,
  NAV_ICON_TURN_LEFT,
  NAV_ICON_TURN_RIGHT,
  NAV_ICON_SLIGHT_LEFT,
  NAV_ICON_SLIGHT_RIGHT,
  NAV_ICON_UTURN,
  NAV_ICON_ARRIVE,
  NAV_ICON_MERGE,
  NAV_ICON_RAMP
} nav_icon_id_t;

typedef struct
{
  char action[32];
  char action_icon[48];
  char action_text[NAV_FIELD_MAX];
  char cur_road[NAV_FIELD_MAX];
  char next_road[NAV_FIELD_MAX];
  int32_t remain_distance;
  int32_t next_action_distance;
  int32_t remain_time;
  int32_t icon_type;
  uint64_t timestamp;
  uint8_t valid;
} nav_msg_t;

typedef struct
{
  uint16_t y;
  uint16_t h;
  uint16_t color;
  uint8_t utf8;
} nav_row_layout_t;

typedef struct
{
  uint16_t unicode;
  uint16_t gbk;
  uint8_t mat[NAV_GLYPH16_BYTES];
  uint8_t valid;
} nav_glyph16_cache_t;

static UART_HandleTypeDef *s_huart;
static char s_line_buf[NAV_LINE_MAX];
static uint16_t s_line_len;
static nav_msg_t s_nav;
static nav_msg_t s_nav_pending;
static char s_row_text[NAV_ROW_COUNT][NAV_FIELD_MAX];
static char s_row_cache[NAV_ROW_COUNT][NAV_FIELD_MAX];
static nav_icon_id_t s_icon_id;
static nav_icon_id_t s_icon_cache;
static uint8_t s_bg_ready;
static uint8_t s_static_labels_ready;
static uint16_t s_label_w[NAV_ROW_COUNT];
static uint8_t s_panel_dirty;
static uint8_t s_pending_valid;
static uint32_t s_last_rx_ms;
static uint8_t s_link_lost;
static uint8_t s_force_redraw;
static uint8_t s_paint_prepared;
static uint8_t s_paint_q[NAV_ROW_COUNT];
static uint8_t s_paint_q_count;
static uint8_t s_paint_q_pos;
static nav_glyph16_cache_t s_glyph16_cache[NAV_GLYPH_CACHE_COUNT];
static uint8_t s_glyph16_replace;
static uint8_t s_font_blk[512];

extern uint32_t utf8_to_unicode(char *utf8);
extern uint16_t unicode_to_gbk(uint16_t char_unicode);

static const nav_row_layout_t s_row_layout[NAV_ROW_COUNT] =
{
  { 32U, 52U, RED,      1U },  /* actionText */
  { 88U, 32U, BLACK,    1U },  /* 当前位置 value */
  {124U, 32U, BLACK,    1U },  /* 即将进入 value */
  {160U, 32U, BLACK,    0U },  /* 到下一步的距离 value (ASCII) */
  {196U, 32U, BLACK,    0U },  /* 剩余时间 value (ASCII) */
};

static int nav_json_get_str(const char *json, const char *key, char *out, size_t out_sz)
{
  char needle[48];
  const char *p;
  const char *start;
  size_t i;

  if ((json == NULL) || (key == NULL) || (out == NULL) || (out_sz == 0U))
  {
    return -1;
  }

  out[0] = '\0';
  (void)snprintf(needle, sizeof(needle), "\"%s\":\"", key);
  p = strstr(json, needle);
  if (p == NULL)
  {
    return -1;
  }

  start = p + strlen(needle);
  for (i = 0U; (start[i] != '\0') && (start[i] != '"') && (i + 1U < out_sz); i++)
  {
    out[i] = start[i];
  }
  out[i] = '\0';
  return 0;
}

static int nav_json_get_int(const char *json, const char *key, int32_t *out)
{
  char needle[40];
  const char *p;

  if ((json == NULL) || (key == NULL) || (out == NULL))
  {
    return -1;
  }

  (void)snprintf(needle, sizeof(needle), "\"%s\":", key);
  p = strstr(json, needle);
  if (p == NULL)
  {
    return -1;
  }

  p += strlen(needle);
  while ((*p == ' ') || (*p == '\t'))
  {
    p++;
  }

  if ((*p != '-') && !isdigit((unsigned char)*p))
  {
    return -1;
  }

  *out = (int32_t)strtol(p, NULL, 10);
  return 0;
}

static int nav_json_get_uint64(const char *json, const char *key, uint64_t *out)
{
  char needle[40];
  const char *p;

  if ((json == NULL) || (key == NULL) || (out == NULL))
  {
    return -1;
  }

  (void)snprintf(needle, sizeof(needle), "\"%s\":", key);
  p = strstr(json, needle);
  if (p == NULL)
  {
    return -1;
  }

  p += strlen(needle);
  while ((*p == ' ') || (*p == '\t'))
  {
    p++;
  }

  if (!isdigit((unsigned char)*p))
  {
    return -1;
  }

  *out = strtoull(p, NULL, 10);
  return 0;
}

static uint8_t nav_json_is_navi_update(const char *json)
{
  char type_buf[24];

  if (nav_json_get_str(json, "type", type_buf, sizeof(type_buf)) == 0)
  {
    return (strcmp(type_buf, "navi_update") == 0) ? 1U : 0U;
  }

  return (strstr(json, "navi_update") != NULL) ? 1U : 0U;
}

static int32_t nav_step_distance_meters(const nav_msg_t *nav)
{
  if (nav == NULL)
  {
    return 0;
  }

  return nav->next_action_distance;
}

static uint8_t nav_msg_content_changed(const nav_msg_t *a, const nav_msg_t *b)
{
  if ((a == NULL) || (b == NULL))
  {
    return 1U;
  }

  if (a->next_action_distance != b->next_action_distance)
  {
    return 1U;
  }
  if (a->remain_distance != b->remain_distance)
  {
    return 1U;
  }
  if (a->remain_time != b->remain_time)
  {
    return 1U;
  }
  if (strcmp(a->action_text, b->action_text) != 0)
  {
    return 1U;
  }
  if (strcmp(a->cur_road, b->cur_road) != 0)
  {
    return 1U;
  }
  if (strcmp(a->next_road, b->next_road) != 0)
  {
    return 1U;
  }
  if (strcmp(a->action, b->action) != 0)
  {
    return 1U;
  }

  return 0U;
}

static void nav_uart_recover_errors(void)
{
  if (s_huart == NULL)
  {
    return;
  }

  if ((s_huart->ErrorCode & HAL_UART_ERROR_ORE) != 0U)
  {
    __HAL_UART_CLEAR_OREFLAG(s_huart);
    s_huart->ErrorCode = HAL_UART_ERROR_NONE;
    s_line_len = 0U;
    s_line_buf[0] = '\0';
  }
}

static uint8_t nav_utf8_byte_len(uint8_t b0)
{
  if ((b0 & 0x80U) == 0U)
  {
    return 1U;
  }
  if ((b0 & 0xE0U) == 0xC0U)
  {
    return 2U;
  }
  if ((b0 & 0xF0U) == 0xE0U)
  {
    return 3U;
  }
  if ((b0 & 0xF8U) == 0xF0U)
  {
    return 4U;
  }
  return 1U;
}

static void nav_sanitize_utf8(char *s)
{
  uint8_t *p = (uint8_t *)s;
  uint8_t *w = (uint8_t *)s;

  if (s == NULL)
  {
    return;
  }

  while (*p != 0U)
  {
    uint8_t n = nav_utf8_byte_len(*p);
    uint8_t i;
    uint8_t ok = 1U;

    if ((*p < 0x20U) && (*p != '\n'))
    {
      break;
    }

    if (n > 1U)
    {
      for (i = 1U; i < n; i++)
      {
        if ((p[i] & 0xC0U) != 0x80U)
        {
          ok = 0U;
          break;
        }
      }
      if (ok == 0U)
      {
        break;
      }
    }

    for (i = 0U; i < n; i++)
    {
      *w++ = *p++;
    }
  }

  *w = 0U;
}

static uint8_t nav_str_has(const char *hay, const char *needle)
{
  if ((hay == NULL) || (needle == NULL))
  {
    return 0U;
  }
  return (strstr(hay, needle) != NULL) ? 1U : 0U;
}

static nav_icon_id_t nav_resolve_icon(const char *action, const char *action_icon, int32_t icon_type)
{
  const char *keys[2];
  uint8_t i;

  keys[0] = action_icon;
  keys[1] = action;

  for (i = 0U; i < 2U; i++)
  {
    const char *s = keys[i];

    if ((s == NULL) || (s[0] == '\0'))
    {
      continue;
    }

    if (nav_str_has(s, "uturn") || nav_str_has(s, "u_turn"))
    {
      return NAV_ICON_UTURN;
    }
    if (nav_str_has(s, "sharp_right") || nav_str_has(s, "turn_hard_right"))
    {
      return NAV_ICON_TURN_RIGHT;
    }
    if (nav_str_has(s, "sharp_left") || nav_str_has(s, "turn_hard_left"))
    {
      return NAV_ICON_TURN_LEFT;
    }
    if (nav_str_has(s, "slight_right") || nav_str_has(s, "bear_right") ||
        nav_str_has(s, "keep_right"))
    {
      return NAV_ICON_SLIGHT_RIGHT;
    }
    if (nav_str_has(s, "slight_left") || nav_str_has(s, "bear_left") ||
        nav_str_has(s, "keep_left"))
    {
      return NAV_ICON_SLIGHT_LEFT;
    }
    if (nav_str_has(s, "turn_right") || nav_str_has(s, "right_turn"))
    {
      return NAV_ICON_TURN_RIGHT;
    }
    if (nav_str_has(s, "turn_left") || nav_str_has(s, "left_turn"))
    {
      return NAV_ICON_TURN_LEFT;
    }
    if (nav_str_has(s, "straight") || nav_str_has(s, "continue") ||
        nav_str_has(s, "go_straight") || nav_str_has(s, "head"))
    {
      return NAV_ICON_STRAIGHT;
    }
    if (nav_str_has(s, "arrive") || nav_str_has(s, "destination") ||
        nav_str_has(s, "end") || nav_str_has(s, "reach"))
    {
      return NAV_ICON_ARRIVE;
    }
    if (nav_str_has(s, "merge") || nav_str_has(s, "join"))
    {
      return NAV_ICON_MERGE;
    }
    if (nav_str_has(s, "ramp") || nav_str_has(s, "fork") || nav_str_has(s, "exit"))
    {
      return NAV_ICON_RAMP;
    }
  }

  if (icon_type == 3)
  {
    return NAV_ICON_TURN_RIGHT;
  }

  return NAV_ICON_UNKNOWN;
}

static void nav_format_distance_en(int32_t meters, char *out, size_t out_sz)
{
  if (meters >= 1000)
  {
    (void)snprintf(out, out_sz, "%ld.%ld km",
                   (long)(meters / 1000), (long)((meters % 1000) / 100));
  }
  else
  {
    (void)snprintf(out, out_sz, "%ld m", (long)meters);
  }
}

static void nav_format_eta_hms(int32_t seconds, char *out, size_t out_sz)
{
  int32_t h;
  int32_t m;
  int32_t s;

  if (seconds < 0)
  {
    seconds = 0;
  }

  h = seconds / 3600;
  m = (seconds % 3600) / 60;
  s = seconds % 60;
  (void)snprintf(out, out_sz, "%ldh %ldmin %lds", (long)h, (long)m, (long)s);
}

static uint16_t nav_utf8_text_width(const char *str, uint8_t size)
{
  const uint8_t *p = (const uint8_t *)str;
  uint16_t w = 0U;

  if (str == NULL)
  {
    return 0U;
  }

  while (*p != 0U)
  {
    if (*p < 0x80U)
    {
      w = (uint16_t)(w + (size / 2U));
      p++;
    }
    else
    {
      uint8_t n = nav_utf8_byte_len(*p);
      w = (uint16_t)(w + size);
      p += n;
    }
  }

  return w;
}

static uint8_t nav_link_is_lost(void)
{
  return ((HAL_GetTick() - s_last_rx_ms) >= NAV_LINK_LOST_MS) ? 1U : 0U;
}

static void nav_draw_queue_reset(void)
{
  s_paint_prepared = 0U;
  s_paint_q_count = 0U;
  s_paint_q_pos = 0U;
}

static void nav_request_redraw(uint8_t force_all)
{
  if (force_all != 0U)
  {
    s_force_redraw = 1U;
  }
  s_panel_dirty = 1U;
  nav_draw_queue_reset();
}

static void nav_apply_pending(void)
{
  if (s_pending_valid == 0U)
  {
    return;
  }

  if ((s_nav.valid == 0U) && (s_nav_pending.valid != 0U))
  {
    memset(s_row_cache, 0, sizeof(s_row_cache));
    s_icon_cache = NAV_ICON_UNKNOWN;
  }

  s_nav = s_nav_pending;
}

static void nav_link_update(void)
{
  uint8_t lost = nav_link_is_lost();
  uint8_t was_lost = s_link_lost;

  if (lost != s_link_lost)
  {
    s_link_lost = lost;
    nav_request_redraw(1U);
    if ((was_lost != 0U) && (lost == 0U))
    {
      nav_apply_pending();
      nav_request_redraw(1U);
    }
  }
}

static void nav_draw_thick_line(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color)
{
  rgblcd_draw_line(x1, y1, x2, y2, color);
  rgblcd_draw_line((uint16_t)(x1 + 1U), y1, (uint16_t)(x2 + 1U), y2, color);
  rgblcd_draw_line(x1, (uint16_t)(y1 + 1U), x2, (uint16_t)(y2 + 1U), color);
}

static void nav_draw_arrow_tip(uint16_t tx, uint16_t ty, uint8_t dir, uint16_t color)
{
  switch (dir)
  {
    case 0U:
      nav_draw_thick_line(tx, ty, (uint16_t)(tx - 6U), (uint16_t)(ty + 8U), color);
      nav_draw_thick_line(tx, ty, (uint16_t)(tx + 6U), (uint16_t)(ty + 8U), color);
      break;
    case 1U:
      nav_draw_thick_line(tx, ty, (uint16_t)(tx - 8U), (uint16_t)(ty - 6U), color);
      nav_draw_thick_line(tx, ty, (uint16_t)(tx - 8U), (uint16_t)(ty + 6U), color);
      break;
    case 2U:
      nav_draw_thick_line(tx, ty, (uint16_t)(tx - 6U), (uint16_t)(ty - 8U), color);
      nav_draw_thick_line(tx, ty, (uint16_t)(tx + 6U), (uint16_t)(ty - 8U), color);
      break;
    case 3U:
      nav_draw_thick_line(tx, ty, (uint16_t)(tx + 8U), (uint16_t)(ty - 6U), color);
      nav_draw_thick_line(tx, ty, (uint16_t)(tx + 8U), (uint16_t)(ty + 6U), color);
      break;
    default:
      break;
  }
}

static void nav_draw_icon(nav_icon_id_t id, uint16_t ox, uint16_t oy, uint16_t sz, uint16_t color)
{
  uint16_t cx;
  uint16_t cy;
  uint16_t r;

  rgblcd_fill(ox, oy, (uint16_t)(ox + sz - 1U), (uint16_t)(oy + sz - 1U), WHITE);
  rgblcd_draw_rectangle(ox, oy, (uint16_t)(ox + sz - 1U), (uint16_t)(oy + sz - 1U), LGRAY);

  cx = (uint16_t)(ox + (sz / 2U));
  cy = (uint16_t)(oy + (sz / 2U));
  r = (uint16_t)(sz / 2U - 6U);

  switch (id)
  {
    case NAV_ICON_STRAIGHT:
      nav_draw_thick_line(cx, (uint16_t)(oy + sz - 8U), cx, (uint16_t)(oy + 10U), color);
      nav_draw_arrow_tip(cx, (uint16_t)(oy + 10U), 0U, color);
      break;

    case NAV_ICON_TURN_RIGHT:
      nav_draw_thick_line(cx, (uint16_t)(oy + sz - 8U), cx, (uint16_t)(cy - 2U), color);
      nav_draw_thick_line(cx, (uint16_t)(cy - 2U), (uint16_t)(ox + sz - 10U), (uint16_t)(cy - 2U), color);
      nav_draw_arrow_tip((uint16_t)(ox + sz - 10U), (uint16_t)(cy - 2U), 1U, color);
      break;

    case NAV_ICON_TURN_LEFT:
      nav_draw_thick_line(cx, (uint16_t)(oy + sz - 8U), cx, (uint16_t)(cy - 2U), color);
      nav_draw_thick_line(cx, (uint16_t)(cy - 2U), (uint16_t)(ox + 10U), (uint16_t)(cy - 2U), color);
      nav_draw_arrow_tip((uint16_t)(ox + 10U), (uint16_t)(cy - 2U), 3U, color);
      break;

    case NAV_ICON_SLIGHT_RIGHT:
      nav_draw_thick_line(cx, (uint16_t)(oy + sz - 8U), (uint16_t)(cx + 6U), (uint16_t)(oy + 14U), color);
      nav_draw_arrow_tip((uint16_t)(cx + 10U), (uint16_t)(oy + 12U), 0U, color);
      nav_draw_thick_line((uint16_t)(cx + 8U), (uint16_t)(oy + 18U), (uint16_t)(ox + sz - 10U), (uint16_t)(oy + 24U), color);
      break;

    case NAV_ICON_SLIGHT_LEFT:
      nav_draw_thick_line(cx, (uint16_t)(oy + sz - 8U), (uint16_t)(cx - 6U), (uint16_t)(oy + 14U), color);
      nav_draw_arrow_tip((uint16_t)(cx - 10U), (uint16_t)(oy + 12U), 0U, color);
      nav_draw_thick_line((uint16_t)(cx - 8U), (uint16_t)(oy + 18U), (uint16_t)(ox + 10U), (uint16_t)(oy + 24U), color);
      break;

    case NAV_ICON_UTURN:
      rgblcd_draw_circle((uint16_t)(cx + 4U), (uint16_t)(cy + 2U), (uint8_t)(r - 4U), color);
      nav_draw_thick_line(cx, (uint16_t)(oy + sz - 8U), cx, (uint16_t)(cy + r - 6U), color);
      nav_draw_arrow_tip((uint16_t)(ox + 12U), (uint16_t)(cy + 2U), 3U, color);
      break;

    case NAV_ICON_ARRIVE:
      rgblcd_fill_circle(cx, (uint16_t)(oy + 16U), 6U, color);
      nav_draw_thick_line(cx, (uint16_t)(oy + 22U), cx, (uint16_t)(oy + sz - 8U), color);
      rgblcd_draw_rectangle((uint16_t)(cx - 8U), (uint16_t)(oy + 8U), (uint16_t)(cx + 8U), (uint16_t)(oy + 16U), color);
      break;

    case NAV_ICON_MERGE:
      nav_draw_thick_line((uint16_t)(ox + 12U), (uint16_t)(oy + sz - 8U), cx, (uint16_t)(oy + 12U), color);
      nav_draw_thick_line((uint16_t)(ox + sz - 12U), (uint16_t)(oy + sz - 8U), cx, (uint16_t)(oy + 12U), color);
      nav_draw_arrow_tip(cx, (uint16_t)(oy + 12U), 0U, color);
      break;

    case NAV_ICON_RAMP:
      nav_draw_thick_line((uint16_t)(ox + 10U), (uint16_t)(oy + sz - 8U), (uint16_t)(ox + sz - 14U), (uint16_t)(oy + 14U), color);
      nav_draw_arrow_tip((uint16_t)(ox + sz - 14U), (uint16_t)(oy + 14U), 0U, color);
      nav_draw_thick_line((uint16_t)(ox + sz - 18U), (uint16_t)(oy + 20U), (uint16_t)(ox + sz - 8U), (uint16_t)(oy + 28U), color);
      break;

    default:
      rgblcd_draw_circle(cx, cy, (uint8_t)r, color);
      nav_draw_thick_line(cx, (uint16_t)(cy + 4U), cx, (uint16_t)(oy + 12U), color);
      nav_draw_arrow_tip(cx, (uint16_t)(oy + 12U), 0U, color);
      break;
  }
}

static void nav_paint_ascii(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                            const char *str, uint8_t size, uint16_t color)
{
  char buf[NAV_FIELD_MAX];

  if ((str == NULL) || (str[0] == '\0'))
  {
    return;
  }

  (void)strncpy(buf, str, sizeof(buf) - 1U);
  buf[sizeof(buf) - 1U] = '\0';
  rgblcd_show_string(x, y, w, h, size, buf, color);
}

static void nav_glyph16_load_mat(uint16_t gbk, uint8_t *mat)
{
  uint16_t fdataend;
  uint16_t blkoffset;
  uint16_t rdata;
  uint8_t qh = (uint8_t)(gbk >> 8);
  uint8_t ql = (uint8_t)(gbk & 0xFFU);
  uint8_t i;
  uint32_t offset;
  uint32_t foffset;

  if (mat == NULL)
  {
    return;
  }

  if ((qh < 0x81U) || (ql < 0x40U) || (ql == 0xFFU) || (qh == 0xFFU))
  {
    memset(mat, 0, NAV_GLYPH16_BYTES);
    return;
  }

  if (ql < 0x7FU)
  {
    ql = (uint8_t)(ql - 0x40U);
  }
  else
  {
    ql = (uint8_t)(ql - 0x41U);
  }
  qh = (uint8_t)(qh - 0x81U);

  offset = ((uint32_t)190U * qh + ql) * NAV_GLYPH16_BYTES;
  foffset = offset >> 9;
  blkoffset = (uint16_t)(offset % 512U);
  fdataend = (uint16_t)(blkoffset + NAV_GLYPH16_BYTES);

  if (fdataend <= 512U)
  {
    (void)sd_nand_read_disk(s_font_blk, foffset + ftinfo.f16addr, 1U);
    (void)memcpy(mat, &s_font_blk[blkoffset], NAV_GLYPH16_BYTES);
  }
  else
  {
    rdata = (uint16_t)(fdataend - 512U);
    (void)sd_nand_read_disk(s_font_blk, foffset + ftinfo.f16addr, 1U);
    for (i = 0U; i < NAV_GLYPH16_BYTES; i++)
    {
      if (i == (uint8_t)(NAV_GLYPH16_BYTES - rdata))
      {
        (void)sd_nand_read_disk(s_font_blk, foffset + ftinfo.f16addr + 1U, 1U);
        blkoffset = 0U;
      }
      mat[i] = s_font_blk[blkoffset++];
    }
  }
}

static const uint8_t *nav_glyph16_get(uint16_t unicode)
{
  uint8_t i;
  nav_glyph16_cache_t *entry;
  const uint8_t *builtin;

  for (i = 0U; i < NAV_GLYPH_CACHE_COUNT; i++)
  {
    if ((s_glyph16_cache[i].valid != 0U) && (s_glyph16_cache[i].unicode == unicode))
    {
      return s_glyph16_cache[i].mat;
    }
  }

  builtin = nav_glyph16_builtin_get(unicode);
  if (builtin != NULL)
  {
    return builtin;
  }

  entry = &s_glyph16_cache[s_glyph16_replace];
  s_glyph16_replace = (uint8_t)((s_glyph16_replace + 1U) % NAV_GLYPH_CACHE_COUNT);

  entry->unicode = unicode;
  entry->gbk = unicode_to_gbk(unicode);
  nav_glyph16_load_mat(entry->gbk, entry->mat);
  entry->valid = 1U;
  return entry->mat;
}

static void nav_show_glyph16_cached(uint16_t x, uint16_t y, uint16_t unicode, uint16_t color)
{
  const uint8_t *mat = nav_glyph16_get(unicode);
  uint16_t y0 = y;
  uint8_t t;
  uint8_t t1;
  uint8_t temp;

  for (t = 0U; t < NAV_GLYPH16_BYTES; t++)
  {
    temp = mat[t];
    for (t1 = 0U; t1 < 8U; t1++)
    {
      if ((temp & 0x80U) != 0U)
      {
        rgblcd_draw_point(x, y, color);
      }
      else
      {
        rgblcd_draw_point(x, y, g_back_color);
      }

      temp <<= 1;
      y++;
      if ((y - y0) == 16U)
      {
        y = y0;
        x++;
        if (x >= rgblcddev.width)
        {
          return;
        }
        break;
      }
    }
  }

}

static void nav_paint_utf8(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                           const char *str, uint8_t size, uint16_t color)
{
  uint16_t x0 = x;
  uint16_t y0 = y;
  const uint8_t *p = (const uint8_t *)str;
  char tmp[5];
  uint8_t hz[2];

  if ((str == NULL) || (str[0] == '\0'))
  {
    return;
  }

  while (*p != 0U)
  {
    if (x > (uint16_t)(x0 + w - size))
    {
      y = (uint16_t)(y + size);
      x = x0;
    }
    if (y > (uint16_t)(y0 + h - size))
    {
      break;
    }

    if (*p < 0x80U)
    {
      rgblcd_show_char(x, y, (char)*p, size, 0U, color);
      x = (uint16_t)(x + (size / 2U));
      p++;
    }
    else
    {
      uint8_t n = nav_utf8_byte_len(*p);
      uint32_t uni;

      if (n > 4U)
      {
        n = 1U;
      }
      (void)memcpy(tmp, p, n);
      tmp[n] = '\0';
      uni = utf8_to_unicode(tmp);
      if (size == 16U)
      {
        nav_show_glyph16_cached(x, y, (uint16_t)uni, color);
      }
      else
      {
        uint16_t gbk = unicode_to_gbk((uint16_t)uni);

        hz[0] = (uint8_t)(gbk >> 8);
        hz[1] = (uint8_t)(gbk & 0xFFU);
        text_show_font(x, y, hz, size, 0U, color);
      }
      x = (uint16_t)(x + size);
      p += n;
    }
  }
}

static void nav_draw_static_labels(void)
{
  uint16_t bar_h;
  uint16_t x;
  uint16_t y;
  uint16_t cw;
  static const char *labels[4] = { NAV_LBL_CUR_ZH, NAV_LBL_NEXT_ZH, NAV_LBL_STEP_ZH, NAV_LBL_ETA_ZH };
  uint8_t i;

  if (s_static_labels_ready != 0U)
  {
    return;
  }

  bar_h = ocr_display_bar_height();
  cw = (rgblcddev.width > 16U) ? (uint16_t)(rgblcddev.width - 16U) : rgblcddev.width;
  x = NAV_ICON_PAD;

  for (i = 0U; i < 4U; i++)
  {
    uint8_t row = (uint8_t)(i + 1U);

    y = (uint16_t)(bar_h + s_row_layout[row].y);
    nav_paint_utf8(x, y, cw, s_row_layout[row].h, labels[i], NAV_FONT_SIZE, BLACK);
    s_label_w[row] = nav_utf8_text_width(labels[i], NAV_FONT_SIZE);
  }

  s_static_labels_ready = 1U;
}

static void nav_build_rows(void)
{
  char dist_buf[40];
  char time_buf[40];

  if (s_link_lost != 0U)
  {
    (void)strncpy(s_row_text[0], NAV_LOST, sizeof(s_row_text[0]) - 1U);
    s_row_text[0][sizeof(s_row_text[0]) - 1U] = '\0';
    s_row_text[1][0] = '\0';
    s_row_text[2][0] = '\0';
    s_row_text[3][0] = '\0';
    s_row_text[4][0] = '\0';
    return;
  }

  s_icon_id = nav_resolve_icon(s_nav.action, s_nav.action_icon, s_nav.icon_type);

  if (s_nav.valid == 0U)
  {
    (void)strncpy(s_row_text[0], NAV_WAIT, sizeof(s_row_text[0]) - 1U);
    s_row_text[0][sizeof(s_row_text[0]) - 1U] = '\0';
    s_row_text[1][0] = '\0';
    s_row_text[2][0] = '\0';
    s_row_text[3][0] = '\0';
    s_row_text[4][0] = '\0';
    return;
  }

  s_row_text[0][0] = '\0';
  if (s_nav.action_text[0] != '\0')
  {
    (void)strncpy(s_row_text[0], s_nav.action_text, sizeof(s_row_text[0]) - 1U);
    s_row_text[0][sizeof(s_row_text[0]) - 1U] = '\0';
  }

  if (s_nav.cur_road[0] != '\0')
  {
    (void)strncpy(s_row_text[1], s_nav.cur_road, sizeof(s_row_text[1]) - 1U);
    s_row_text[1][sizeof(s_row_text[1]) - 1U] = '\0';
  }
  else
  {
    s_row_text[1][0] = '\0';
  }

  if (s_nav.next_road[0] != '\0')
  {
    (void)strncpy(s_row_text[2], s_nav.next_road, sizeof(s_row_text[2]) - 1U);
    s_row_text[2][sizeof(s_row_text[2]) - 1U] = '\0';
  }
  else
  {
    s_row_text[2][0] = '\0';
  }

  nav_format_distance_en(nav_step_distance_meters(&s_nav), dist_buf, sizeof(dist_buf));
  (void)strncpy(s_row_text[3], dist_buf, sizeof(s_row_text[3]) - 1U);
  s_row_text[3][sizeof(s_row_text[3]) - 1U] = '\0';

  nav_format_eta_hms(s_nav.remain_time, time_buf, sizeof(time_buf));
  (void)strncpy(s_row_text[4], time_buf, sizeof(s_row_text[4]) - 1U);
  s_row_text[4][sizeof(s_row_text[4]) - 1U] = '\0';
}

static void nav_paint_row(uint8_t row)
{
  uint16_t bar_h;
  uint16_t x;
  uint16_t y;
  uint16_t w;
  uint16_t cw;
  uint16_t text_x;

  if (row >= NAV_ROW_COUNT)
  {
    return;
  }

  bar_h = ocr_display_bar_height();
  cw = (rgblcddev.width > 16U) ? (uint16_t)(rgblcddev.width - 16U) : rgblcddev.width;
  y = (uint16_t)(bar_h + s_row_layout[row].y);
  w = cw;

  if (row == 0U)
  {
    x = NAV_ICON_PAD;
    rgblcd_fill(x, y, (uint16_t)(x + NAV_ICON_BOX - 1U), (uint16_t)(y + s_row_layout[row].h - 1U), WHITE);
    if ((s_nav.valid != 0U) && (s_link_lost == 0U))
    {
      if (s_icon_id != s_icon_cache)
      {
        nav_draw_icon(s_icon_id, x, y, NAV_ICON_BOX, s_row_layout[row].color);
        s_icon_cache = s_icon_id;
      }
    }

    text_x = NAV_TEXT_X;
    rgblcd_fill(text_x, y, (uint16_t)(text_x + w - (text_x - NAV_ICON_PAD) - 1U),
                (uint16_t)(y + s_row_layout[row].h - 1U), WHITE);
    if (s_row_text[row][0] != '\0')
    {
      nav_paint_utf8(text_x, y, (uint16_t)(w - (text_x - NAV_ICON_PAD)), s_row_layout[row].h,
                     s_row_text[row], NAV_FONT_SIZE, s_row_layout[row].color);
    }
    return;
  }

  if (s_static_labels_ready != 0U)
  {
    uint16_t value_x = (uint16_t)(NAV_ICON_PAD + s_label_w[row]);
    uint16_t value_w = (s_label_w[row] < w) ? (uint16_t)(w - s_label_w[row]) : 0U;

    if (value_w == 0U)
    {
      return;
    }

    rgblcd_fill(value_x, y, (uint16_t)(value_x + value_w - 1U),
                (uint16_t)(y + s_row_layout[row].h - 1U), WHITE);

    if (s_row_text[row][0] == '\0')
    {
      return;
    }

    if (s_row_layout[row].utf8 != 0U)
    {
      nav_paint_utf8(value_x, y, value_w, s_row_layout[row].h,
                     s_row_text[row], NAV_FONT_SIZE, s_row_layout[row].color);
    }
    else
    {
      nav_paint_ascii(value_x, y, value_w, s_row_layout[row].h,
                      s_row_text[row], NAV_FONT_SIZE, s_row_layout[row].color);
    }
    return;
  }

  x = NAV_ICON_PAD;
  rgblcd_fill(x, y, (uint16_t)(x + w - 1U), (uint16_t)(y + s_row_layout[row].h - 1U), WHITE);

  if (s_row_text[row][0] == '\0')
  {
    return;
  }

  if (s_row_layout[row].utf8 != 0U)
  {
    nav_paint_utf8(x, y, w, s_row_layout[row].h, s_row_text[row], NAV_FONT_SIZE, s_row_layout[row].color);
  }
  else
  {
    nav_paint_ascii(x, y, w, s_row_layout[row].h, s_row_text[row], NAV_FONT_SIZE, s_row_layout[row].color);
  }
}

static void nav_draw_bg_once(void)
{
  uint16_t bar_h;
  uint16_t cw;

  if (s_bg_ready != 0U)
  {
    return;
  }

  bar_h = ocr_display_bar_height();
  rgblcd_fill(0U, bar_h, (uint16_t)(rgblcddev.width - 1U),
              (uint16_t)(rgblcddev.height - 1U), WHITE);
  cw = (rgblcddev.width > 16U) ? (uint16_t)(rgblcddev.width - 16U) : rgblcddev.width;
  nav_paint_ascii(NAV_ICON_PAD, (uint16_t)(bar_h + 4U), cw, 24U, NAV_TITLE, NAV_FONT_SIZE, BLUE);
  nav_draw_static_labels();
  s_bg_ready = 1U;
}

static void nav_draw_prepare(void)
{
  uint8_t row;
  uint8_t force_all;

  nav_draw_bg_once();
  nav_build_rows();

  force_all = (s_force_redraw != 0U);
  if (force_all == 0U)
  {
    force_all = (s_nav.valid != 0U) && (s_link_lost == 0U) &&
                ((s_row_cache[0][0] == '\0') ||
                 (strstr(s_row_cache[0], NAV_WAIT) != NULL) ||
                 (strstr(s_row_cache[0], NAV_LOST) != NULL));
  }

  if (force_all != 0U)
  {
    s_icon_cache = NAV_ICON_UNKNOWN;
  }

  s_paint_q_count = 0U;
  s_paint_q_pos = 0U;

  for (row = 0U; row < NAV_ROW_COUNT; row++)
  {
    uint8_t paint = 0U;

    if (s_row_text[row][0] == '\0')
    {
      if ((force_all != 0U) || (s_row_cache[row][0] != '\0'))
      {
        s_row_cache[row][0] = '\0';
        paint = 1U;
      }
    }
    else if (force_all != 0U)
    {
      paint = 1U;
    }
    else if (row == 0U)
    {
      if ((strcmp(s_row_text[row], s_row_cache[row]) != 0) ||
          (s_icon_id != s_icon_cache))
      {
        paint = 1U;
      }
    }
    else if (strcmp(s_row_text[row], s_row_cache[row]) != 0)
    {
      paint = 1U;
    }

    if (paint != 0U)
    {
      s_paint_q[s_paint_q_count++] = row;
    }
  }

  s_force_redraw = 0U;
  s_paint_prepared = 1U;
}

static void nav_draw_step(void)
{
  uint8_t row;

  if (s_paint_prepared == 0U)
  {
    return;
  }

  if (s_paint_q_pos >= s_paint_q_count)
  {
    s_paint_prepared = 0U;
    s_panel_dirty = 0U;
    return;
  }

  row = s_paint_q[s_paint_q_pos++];
  (void)strncpy(s_row_cache[row], s_row_text[row], sizeof(s_row_cache[row]) - 1U);
  s_row_cache[row][sizeof(s_row_cache[row]) - 1U] = '\0';
  nav_paint_row(row);
}

static void nav_draw_panel(void)
{
  while (s_panel_dirty != 0U)
  {
    if (s_paint_prepared == 0U)
    {
      nav_draw_prepare();
    }
    nav_draw_step();
    if ((s_paint_prepared == 0U) && (s_panel_dirty != 0U))
    {
      break;
    }
  }
}

static uint8_t nav_parse_line(const char *line)
{
  nav_msg_t msg;
  size_t len;
  char last;
  uint8_t was_lost;

  if ((line == NULL) || (line[0] != '{'))
  {
    return 0U;
  }

  if (nav_json_is_navi_update(line) == 0U)
  {
    return 0U;
  }

  len = strlen(line);
  if (len < 10U)
  {
    return 0U;
  }

  last = line[len - 1U];
  while ((last == ' ') || (last == '\t'))
  {
    len--;
    if (len == 0U)
    {
      return 0U;
    }
    last = line[len - 1U];
  }
  if (last != '}')
  {
    return 0U;
  }

  memset(&msg, 0, sizeof(msg));
  (void)nav_json_get_uint64(line, "timestamp", &msg.timestamp);
  (void)nav_json_get_str(line, "actionText", msg.action_text, sizeof(msg.action_text));
  if (msg.action_text[0] == '\0')
  {
    (void)nav_json_get_str(line, "text", msg.action_text, sizeof(msg.action_text));
  }
  (void)nav_json_get_str(line, "curRoad", msg.cur_road, sizeof(msg.cur_road));
  (void)nav_json_get_str(line, "nextRoad", msg.next_road, sizeof(msg.next_road));
  (void)nav_json_get_str(line, "action", msg.action, sizeof(msg.action));
  (void)nav_json_get_int(line, "nextActionDistance", &msg.next_action_distance);
  (void)nav_json_get_int(line, "remainTime", &msg.remain_time);
  /* legacy / optional fields */
  (void)nav_json_get_str(line, "actionIcon", msg.action_icon, sizeof(msg.action_icon));
  (void)nav_json_get_int(line, "remainDistance", &msg.remain_distance);
  if (msg.next_action_distance == 0)
  {
    (void)nav_json_get_int(line, "curStepRemainDistance", &msg.next_action_distance);
  }
  (void)nav_json_get_int(line, "iconType", &msg.icon_type);

  nav_sanitize_utf8(msg.action_text);
  nav_sanitize_utf8(msg.cur_road);
  nav_sanitize_utf8(msg.next_road);

  msg.valid = 1U;
  s_last_rx_ms = HAL_GetTick();
  s_nav_pending = msg;
  s_pending_valid = 1U;
  was_lost = s_link_lost;
  if (s_link_lost != 0U)
  {
    s_link_lost = 0U;
  }

  if (was_lost != 0U)
  {
    nav_apply_pending();
    nav_request_redraw(1U);
  }
  else if (nav_msg_content_changed(&s_nav_pending, &s_nav) != 0U)
  {
    uint8_t first_msg = (s_nav.valid == 0U) ? 1U : 0U;

    nav_apply_pending();
    nav_request_redraw(first_msg);
  }

  return 1U;
}

void nav_uart_init(void)
{
  s_huart = uart_port_handle();
  s_line_len = 0U;
  s_line_buf[0] = '\0';
  s_bg_ready = 0U;
  s_static_labels_ready = 0U;
  memset(s_label_w, 0, sizeof(s_label_w));
  s_panel_dirty = 1U;
  s_icon_id = NAV_ICON_UNKNOWN;
  s_icon_cache = NAV_ICON_UNKNOWN;
  memset(&s_nav, 0, sizeof(s_nav));
  memset(&s_nav_pending, 0, sizeof(s_nav_pending));
  memset(s_row_cache, 0, sizeof(s_row_cache));
  s_pending_valid = 0U;
  s_last_rx_ms = HAL_GetTick();
  s_link_lost = 0U;
  s_force_redraw = 1U;
  nav_draw_queue_reset();
  nav_draw_panel();
}

void nav_uart_redraw(void)
{
  nav_request_redraw(1U);
  nav_draw_panel();
}

static void nav_uart_feed_byte(uint8_t ch)
{
  if ((ch == '\r') || (ch == '\n'))
  {
    if (s_line_len > 0U)
    {
      s_line_buf[s_line_len] = '\0';
      (void)nav_parse_line(s_line_buf);
      s_line_len = 0U;
    }
    return;
  }

  if ((ch < 0x20U) && (ch != '\t'))
  {
    return;
  }

  if (s_line_len + 1U >= NAV_LINE_MAX)
  {
    s_line_len = 0U;
    s_line_buf[0] = '\0';
    if (ch == '{')
    {
      s_line_buf[s_line_len++] = (char)ch;
    }
    return;
  }

  s_line_buf[s_line_len++] = (char)ch;
}

void nav_uart_poll(void)
{
  uint8_t ch;
  uint16_t rx_budget = NAV_RX_BUDGET_BYTES;

  if (s_huart == NULL)
  {
    s_huart = uart_port_handle();
    if (s_huart == NULL)
    {
      return;
    }
  }

  nav_uart_recover_errors();

  while ((rx_budget > 0U) && (HAL_UART_Receive(s_huart, &ch, 1U, NAV_RX_TIMEOUT_MS) == HAL_OK))
  {
    nav_uart_feed_byte(ch);
    rx_budget--;
  }

  nav_uart_recover_errors();

  nav_link_update();

  if (s_panel_dirty != 0U)
  {
    if (s_paint_prepared == 0U)
    {
      nav_draw_prepare();
    }
    nav_draw_step();
  }
}
