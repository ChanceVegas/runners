// sprite.cpp — see sprite.h. Inner loop: 16.16 fixed-point source stepping, one byte
// read + one 16-bit store per opaque pixel. A full-size runner (40x70) is ~2.8k px;
// the nearest obstacle (~120x172) ~20k px — well under 1 ms per frame on the S3.
#include "sprite.h"
#include "renderer.h"
#include "board_config.h"

namespace {
uint16_t s_pal[ART_PALETTE_N];      // band-buffer order
}

namespace sprite {

bool init() {
  for (int i = 0; i < ART_PALETTE_N; ++i) s_pal[i] = renderer::raw(ART_PALETTE[i]);
  return true;
}

void draw(lgfx::LGFX_Sprite& band, int32_t bandY, const ArtSprite& s, int32_t x, int32_t y,
          int32_t dw, int32_t dh, bool flipX, uint16_t solid) {
  if (dw <= 0 || dh <= 0) return;
  const int32_t bh = band.height();
  int32_t y0 = y < bandY ? bandY : y;
  int32_t y1 = y + dh;
  if (y1 > bandY + bh) y1 = bandY + bh;
  if (y1 > LCD_HEIGHT) y1 = LCD_HEIGHT;
  if (y0 >= y1) return;
  int32_t x0 = x < 0 ? 0 : x;
  int32_t x1 = x + dw > LCD_WIDTH ? LCD_WIDTH : x + dw;
  if (x0 >= x1) return;

  uint16_t* buf = (uint16_t*)band.getBuffer();
  uint16_t pal[ART_PALETTE_N];                       // solid: every index -> one colour
  const uint16_t* P = s_pal;
  if (solid) {
    const uint16_t c = renderer::raw(solid);
    for (int i = 0; i < ART_PALETTE_N; ++i) pal[i] = c;
    P = pal;
  }
  const uint32_t stepX = ((uint32_t)s.w << 16) / (uint32_t)dw;
  const uint32_t startFx = (uint32_t)(x0 - x) * stepX + (stepX >> 1);   // sample pixel centres
  for (int32_t sy = y0; sy < y1; ++sy) {
    const int32_t srcY = (int32_t)(((int64_t)(sy - y) * s.h) / dh);
    const uint8_t* src = s.px + srcY * s.w;
    uint16_t* dst = buf + (sy - bandY) * LCD_WIDTH;
    uint32_t fx = startFx;
    if (!flipX) {
      for (int32_t dx = x0; dx < x1; ++dx, fx += stepX) {
        const uint8_t idx = src[fx >> 16];
        if (idx) dst[dx] = P[idx];
      }
    } else {
      const int32_t last = s.w - 1;
      for (int32_t dx = x0; dx < x1; ++dx, fx += stepX) {
        const uint8_t idx = src[last - (int32_t)(fx >> 16)];
        if (idx) dst[dx] = P[idx];
      }
    }
  }
}

}
