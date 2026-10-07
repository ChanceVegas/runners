// hud.cpp — see hud.h. text() tests the band from the font height BEFORE measuring
// the string (textWidth walks every glyph), so most bands skip each string cheaply.
#include "hud.h"
#include "color.h"

namespace hud {

const lgfx::IFont* const F9  = &fonts::FreeSansBold9pt7b;
const lgfx::IFont* const F12 = &fonts::FreeSansBold12pt7b;
const lgfx::IFont* const F18 = &fonts::FreeSansBold18pt7b;
const lgfx::IFont* const F24 = &fonts::FreeSansBold24pt7b;

void rect(lgfx::LGFX_Sprite& b, int32_t bandY, int32_t x, int32_t y, int32_t w, int32_t h,
          uint16_t c) {
  if (w <= 0 || h <= 0 || !rowsHit(y, y + h, bandY, b.height())) return;
  b.fillRect(x, y - bandY, w, h, c);
}

void frame(lgfx::LGFX_Sprite& b, int32_t bandY, int32_t x, int32_t y, int32_t w, int32_t h,
           uint16_t c) {
  rect(b, bandY, x, y, w, 2, c);
  rect(b, bandY, x, y + h - 2, w, 2, c);
  rect(b, bandY, x, y, 2, h, c);
  rect(b, bandY, x + w - 2, y, 2, h, c);
}

void text(lgfx::LGFX_Sprite& b, int32_t bandY, const char* str, int32_t x, int32_t y,
          const lgfx::IFont* font, float size, uint16_t color, int32_t maxW) {
  b.setFont(font);
  b.setTextSize(size);
  const int32_t half = b.fontHeight() / 2 + 3;
  if (!rowsHit(y - half, y + half, bandY, b.height())) return;
  const int32_t w = b.textWidth(str);
  if (w > maxW && w > 0) { size *= (float)maxW / w; b.setTextSize(size); }
  b.setTextDatum(lgfx::middle_center);
  b.setTextColor(rgb565(0, 0, 0));
  b.drawString(str, x + 2, y - bandY + 2);
  b.setTextColor(color);
  b.drawString(str, x, y - bandY);
}

} // namespace hud
