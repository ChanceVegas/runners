// hud.h — shared band-clipped drawing helpers for HUD/UI layers: filled rects and
// centred drop-shadow text (smooth FreeSansBold fonts, auto-shrink to fit). Every
// helper skips bands it doesn't touch, so callers can draw "whole screen" UI from a
// per-band compose callback without wasting calls.
#pragma once
#include <LovyanGFX.hpp>
#include <stdint.h>

namespace hud {

inline bool rowsHit(int32_t y0, int32_t y1, int32_t bandY, int32_t bandH) {
  return y1 > bandY && y0 < bandY + bandH;
}

// Screen-space filled rect (native RGB565), clipped to the band.
void rect(lgfx::LGFX_Sprite& b, int32_t bandY, int32_t x, int32_t y, int32_t w, int32_t h,
          uint16_t c);

// Screen-space rect outline, 2 px thick.
void frame(lgfx::LGFX_Sprite& b, int32_t bandY, int32_t x, int32_t y, int32_t w, int32_t h,
           uint16_t c);

// Centred text with a drop shadow at screen (x, y), shrunk to fit maxW.
void text(lgfx::LGFX_Sprite& b, int32_t bandY, const char* str, int32_t x, int32_t y,
          const lgfx::IFont* font, float size, uint16_t color, int32_t maxW = 464);

// Fonts used across the UI.
extern const lgfx::IFont* const F9;
extern const lgfx::IFont* const F12;
extern const lgfx::IFont* const F18;
extern const lgfx::IFont* const F24;

}
