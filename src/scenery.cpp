// scenery.cpp — props sit every SCENERY_SPACING_M metres on each side of the road
// (sides offset by half a spacing). Slot k's kind/size/offset come from a hash of k,
// so the same stretch of road always looks the same and nothing needs storing.
// Screen positions are projected once per frame in beginFrame, not once per band.
#include "scenery.h"
#include "lanes.h"
#include "config.h"
#include "color.h"
#include "board_config.h"
#include <math.h>

namespace {

enum class Kind : uint8_t { Pine, TallPine, Bush, Post };

struct Prop {
  Kind kind;
  int16_t x, y;      // screen: centre x, ground y
  float s;           // scale
};

constexpr int MAX_PROPS = 2 * ((int)(SCENERY_FAR_M / SCENERY_SPACING_M) + 2);
Prop s_props[MAX_PROPS];
int s_n = 0;

inline uint32_t hash(uint32_t k) {
  k ^= k >> 16; k *= 0x7feb352dU; k ^= k >> 15; k *= 0x846ca68bU; k ^= k >> 16;
  return k;
}

inline bool rowsHit(int32_t y0, int32_t y1, int32_t bandY, int32_t bandH) {
  return y1 > bandY && y0 < bandY + bandH;
}

constexpr uint16_t PINE_DARK  = rgb565(28, 92, 44);
constexpr uint16_t PINE_LIGHT = rgb565(46, 124, 58);
constexpr uint16_t TRUNK      = rgb565(92, 62, 32);
constexpr uint16_t BUSH       = rgb565(58, 138, 50);
constexpr uint16_t BUSH_HI    = rgb565(96, 176, 70);
constexpr uint16_t POST       = rgb565(235, 235, 230);
constexpr uint16_t REFLECTOR  = rgb565(230, 40, 30);

} // namespace

namespace scenery {

void beginFrame(float travel) {
  s_n = 0;
  // Slots from farthest to nearest so the list is already in painter's order.
  const int32_t kFar  = (int32_t)floorf((travel + SCENERY_FAR_M) / SCENERY_SPACING_M);
  const int32_t kNear = (int32_t)floorf((travel + 2.0f) / SCENERY_SPACING_M);
  for (int32_t k = kFar; k >= kNear && s_n < MAX_PROPS - 1; --k) {
    for (int side = 0; side < 2; ++side) {
      const uint32_t h = hash((uint32_t)k * 2u + (uint32_t)side);
      if ((h & 7) == 0) continue;                       // ~1 in 8 slots left empty
      const float wz = (k + (side ? 0.5f : 0.0f)) * SCENERY_SPACING_M;
      const float z = wz - travel;
      if (z < 2.0f || z > SCENERY_FAR_M) continue;
      const float off = SCENERY_LANE_OFFSET + ((h >> 8) & 15) / 15.0f * SCENERY_LANE_JITTER;
      float sx, sy, s;
      if (!lanes::project(z, side ? off : -off, sx, sy, s)) continue;
      if (s < 0.02f) continue;
      Kind kind;
      switch ((h >> 4) & 7) {
        case 0: case 1: case 2: kind = Kind::Pine; break;
        case 3: case 4:         kind = Kind::TallPine; break;
        case 5: case 6:         kind = Kind::Bush; break;
        default:                kind = Kind::Post; break;
      }
      s_props[s_n++] = { kind, (int16_t)sx, (int16_t)sy, s };
    }
  }
}

void composeBand(lgfx::LGFX_Sprite& b, int32_t bandY) {
  const int32_t bh = b.height();
  for (int i = 0; i < s_n; ++i) {
    const Prop& p = s_props[i];
    const float s = p.s;
    const int32_t x = p.x, y = p.y;
    switch (p.kind) {
      case Kind::Pine:
      case Kind::TallPine: {
        const int32_t h  = (int32_t)((p.kind == Kind::TallPine ? 190.0f : 130.0f) * s) + 2;
        const int32_t w  = (int32_t)(70.0f * s) + 2;
        const int32_t th = h / 6 + 1, tw = w / 6 + 1;
        if (!rowsHit(y - h, y, bandY, bh)) break;
        b.fillRect(x - tw / 2, y - th - bandY, tw, th, TRUNK);
        b.fillTriangle(x, y - h - bandY, x - w / 2, y - th - bandY, x + w / 2, y - th - bandY,
                       PINE_DARK);
        b.fillTriangle(x, y - h - bandY, x - w / 4, y - h / 2 - bandY, x + w / 3, y - th - bandY,
                       PINE_LIGHT);                     // lit flank
        break;
      }
      case Kind::Bush: {
        const int32_t rx = (int32_t)(40.0f * s) + 1, ry = (int32_t)(24.0f * s) + 1;
        if (!rowsHit(y - 2 * ry, y, bandY, bh)) break;
        b.fillEllipse(x, y - ry - bandY, rx, ry, BUSH);
        b.fillEllipse(x - rx / 4, y - ry - ry / 3 - bandY, rx / 2 + 1, ry / 2 + 1, BUSH_HI);
        break;
      }
      case Kind::Post: {
        const int32_t h = (int32_t)(60.0f * s) + 2, w = (int32_t)(8.0f * s) + 1;
        if (!rowsHit(y - h, y, bandY, bh)) break;
        b.fillRect(x - w / 2, y - h - bandY, w, h, POST);
        b.fillRect(x - w / 2, y - h + h / 6 - bandY, w, h / 6 + 1, REFLECTOR);
        break;
      }
    }
  }
}

} // namespace scenery
