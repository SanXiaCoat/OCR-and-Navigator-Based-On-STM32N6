#ifndef NAV_GLYPHS_BUILTIN_H
#define NAV_GLYPHS_BUILTIN_H

#include <stdint.h>

#define NAV_GLYPH16_BYTES     32U
#define NAV_BUILTIN_GLYPH_COUNT 65U

typedef struct
{
  uint16_t unicode;
  const uint8_t mat[NAV_GLYPH16_BYTES];
} nav_builtin_glyph_t;

const uint8_t *nav_glyph16_builtin_get(uint16_t unicode);

#endif /* NAV_GLYPHS_BUILTIN_H */
