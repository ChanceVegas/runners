// lanes.cpp — per-scanline pseudo-3D road (OutRun / Pole Position technique).
// Row y below the horizon: d = y - HORIZON, z = K/d (m), s = d/ROWS. Road
// half-width = ROAD_HALF_PX * s; stripe colour alternates on floor((z+travel)/SEG).
// Bend: centre x = 240 + curve * (1-s)^2, so the road bends near the horizon and
// stays centred at the runner's feet.
#include "lanes.h"
#include "renderer.h"
#include "config.h"
#include "board_config.h"
#include <Arduino.h>
#include <math.h>

namespace {


inline uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}
inline uint16_t raw(uint16_t c) { return renderer::raw(c); }   // buffer order (R1-BYTE)

// Palette, pre-converted to buffer order at init.
uint16_t s_sky[ROAD_HORIZON_Y];        // vertical sky gradient
uint16_t s_hillFar, s_hillNear;
uint16_t s_grass[2], s_road[2], s_rumble[2], s_line;

// Hill silhouettes: heights above the horizon, 1024-px wrap-around tables.
constexpr int RIDGE_N = 1024;
uint8_t s_ridgeFar[RIDGE_N], s_ridgeNear[RIDGE_N];

// Per-frame state.
float s_curve = 0.0f;                  // px bend at the horizon this frame
int32_t s_bgFar = 0, s_bgNear = 0;     // hill scroll offsets, px

// Per-row road tables (index = d, 1..ROAD_ROWS), filled by beginFrame.
int16_t s_cx[ROAD_ROWS + 1];
int16_t s_half[ROAD_ROWS + 1];
uint8_t s_phase[ROAD_ROWS + 1];

inline void fill(uint16_t* row, int32_t x0, int32_t x1, uint16_t c) {
  if (x0 < 0) x0 = 0;
  if (x1 > LCD_WIDTH) x1 = LCD_WIDTH;
  for (int32_t x = x0; x < x1; ++x) row[x] = c;
}

} // namespace

namespace lanes {

bool init() {

  for (int y = 0; y < ROAD_HORIZON_Y; ++y) {          // deep blue -> warm horizon
    float t = (float)y / (ROAD_HORIZON_Y - 1);
    s_sky[y] = raw(rgb(40 + (int)(200 * t), 90 + (int)(110 * t), 190 - (int)(10 * t)));
  }
  s_hillFar  = raw(rgb(110, 130, 170));
  s_hillNear = raw(rgb(60, 110, 80));
  s_grass[0] = raw(rgb(70, 150, 60));   s_grass[1] = raw(rgb(60, 130, 52));
  s_road[0]  = raw(rgb(105, 105, 110)); s_road[1]  = raw(rgb(96, 96, 102));
  s_rumble[0] = raw(rgb(220, 40, 40));  s_rumble[1] = raw(rgb(240, 240, 240));
  s_line = raw(rgb(245, 245, 235));

  for (int i = 0; i < RIDGE_N; ++i) {                 // sums of sines, seamless wrap
    float a = i * (2.0f * (float)M_PI / RIDGE_N);
    s_ridgeFar[i]  = (uint8_t)(34 + 14 * sinf(a * 3) + 8 * sinf(a * 7 + 1.3f) + 4 * sinf(a * 17));
    s_ridgeNear[i] = (uint8_t)(16 + 9 * sinf(a * 5 + 0.7f) + 5 * sinf(a * 11 + 2.1f) + 3 * sinf(a * 23));
  }
  return true;
}

void beginFrame(float travel) {
  const float w = travel / ROAD_CURVE_PERIOD_M;
  s_curve = ROAD_CURVE_PX * sinf(w * 2.0f * (float)M_PI);
  // Hill offset = integral of the bend over distance, so hills drift opposite the
  // bend and stop when the road straightens. Closed form keeps it interpolation-safe.
  float drift = -ROAD_CURVE_PX * ROAD_CURVE_PERIOD_M / (2.0f * (float)M_PI)
                * cosf(w * 2.0f * (float)M_PI) * ROAD_BG_PARALLAX;
  s_bgFar  = (int32_t)(drift * 0.5f);
  s_bgNear = (int32_t)drift;

  for (int d = 1; d <= ROAD_ROWS; ++d) {
    float s = (float)d / ROAD_ROWS;
    float z = ROAD_CAM_K / d;
    float k = 1.0f - s;
    s_cx[d]   = (int16_t)(LCD_WIDTH / 2 + s_curve * k * k);
    s_half[d] = (int16_t)(ROAD_HALF_PX * s);
    s_phase[d] = (uint8_t)(((int32_t)((z + travel) / ROAD_SEG_M)) & 1);
  }
}

void composeBand(lgfx::LGFX_Sprite& band, int32_t bandY) {
  uint16_t* buf = (uint16_t*)band.getBuffer();
  const int32_t h = band.height();

  for (int32_t r = 0; r < h; ++r) {
    const int32_t y = bandY + r;
    if (y >= LCD_HEIGHT) break;
    uint16_t* row = buf + r * LCD_WIDTH;

    if (y < ROAD_HORIZON_Y) {                       // sky + two hill ridges
      const uint16_t sky = s_sky[y];
      const int32_t above = ROAD_HORIZON_Y - y;     // rows above the horizon
      for (int32_t x = 0; x < LCD_WIDTH; ++x) {
        uint16_t c = sky;
        if (above <= s_ridgeFar[(x + s_bgFar) & (RIDGE_N - 1)]) c = s_hillFar;
        if (above <= s_ridgeNear[(x + s_bgNear) & (RIDGE_N - 1)]) c = s_hillNear;
        row[x] = c;
      }
      continue;
    }

    int32_t d = y - ROAD_HORIZON_Y;
    if (d < 1) d = 1;
    const int32_t cx = s_cx[d], half = s_half[d];
    const uint8_t ph = s_phase[d];

    fill(row, 0, LCD_WIDTH, s_grass[ph]);                       // grass
    fill(row, cx - half, cx + half, s_road[ph]);                // asphalt
    const int32_t rum = half / 12 + 1;                          // rumble strips
    fill(row, cx - half - rum, cx - half, s_rumble[ph]);
    fill(row, cx + half, cx + half + rum, s_rumble[ph]);
    if (ph == 0) {                                              // dashed lane lines
      const int32_t lw = half / 40 + 1;
      const int32_t l1 = cx - half / 3, l2 = cx + half / 3;
      fill(row, l1 - lw, l1 + lw, s_line);
      fill(row, l2 - lw, l2 + lw, s_line);
    }
  }
}

bool project(float z, float lane, float& sx, float& sy, float& s) {
  if (z <= 0.05f) return false;
  const float d = ROAD_CAM_K / z;
  if (d < 0.5f) return false;                     // beyond the horizon
  s = d / ROAD_ROWS;
  const float k = 1.0f - s;
  const float cx = LCD_WIDTH / 2 + s_curve * k * k;
  sx = cx + lane * (2.0f / 3.0f) * ROAD_HALF_PX * s;   // lane centres at ±2/3 half-width
  sy = ROAD_HORIZON_Y + d;
  return true;
}

float depthAtRow(int32_t y) {
  int32_t d = y - ROAD_HORIZON_Y;
  if (d < 1) d = 1;
  return ROAD_CAM_K / d;
}

} // namespace lanes
