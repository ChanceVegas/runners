// color.h — compile-time RGB888 -> RGB565 helper for library draw calls
// (fillRect etc. take NATIVE RGB565; only direct band-buffer writes need swapping,
// see lanes.cpp / R1-BYTE).
#pragma once
#include <stdint.h>

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}
