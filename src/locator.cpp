// locator.cpp — simulated position source. Drag from where the finger lands sets a
// direction; drag length picks walk vs run pace (LOC_SIM_*). Movement is sped up by
// LOC_SIM_TIME_SCALE for desk testing, but speedMS() reports the REAL-world pace so
// movement classes, energy and engage rules behave as they will with GPS.
#include "locator.h"
#include "input.h"
#include "config.h"
#include <math.h>

namespace {

locator::Pos s_pos = {0, 0, 0.0f, 0.0f};
float s_speed = 0.0f;
float s_heading = 0.0f;
bool  s_enabled = false;

void normalize(locator::Pos& p) {
  while (p.fx >= OW_TILE_M) { p.fx -= OW_TILE_M; ++p.tx; }
  while (p.fx < 0.0f)       { p.fx += OW_TILE_M; --p.tx; }
  while (p.fy >= OW_TILE_M) { p.fy -= OW_TILE_M; ++p.ty; }
  while (p.fy < 0.0f)       { p.fy += OW_TILE_M; --p.ty; }
}

} // namespace

namespace locator {

bool init(const Pos& start) {
  s_pos = start;
  normalize(s_pos);
  s_speed = 0.0f;
  return true;
}

void setEnabled(bool on) {
  s_enabled = on;
  if (!on) s_speed = 0.0f;
}

void update(float dt) {
  if (!s_enabled) return;
  const input::State& in = input::state();
  float vx = in.moveX, vy = in.moveY;
  const float mag = sqrtf(vx * vx + vy * vy);
  if (!in.touching || mag < 0.05f) { s_speed = 0.0f; return; }

  const float pace = (mag >= LOC_SIM_RUN_DRAG) ? LOC_SIM_RUN_MS : LOC_SIM_WALK_MS;
  vx /= mag; vy /= mag;
  s_heading = atan2f(vy, vx);
  s_speed = pace;
  s_pos.fx += vx * pace * LOC_SIM_TIME_SCALE * dt;
  s_pos.fy += vy * pace * LOC_SIM_TIME_SCALE * dt;
  normalize(s_pos);
}

Pos   pos()        { return s_pos; }
float speedMS()    { return s_speed; }
float headingRad() { return s_heading; }

Move move() {
  if (s_speed < LOC_STILL_MS) return Move::Still;
  return (s_speed >= LOC_RUN_MS) ? Move::Run : Move::Walk;
}

void deltaM(const Pos& a, const Pos& b, float& dx, float& dy) {
  dx = (float)(b.tx - a.tx) * OW_TILE_M + (b.fx - a.fx);
  dy = (float)(b.ty - a.ty) * OW_TILE_M + (b.fy - a.fy);
}

} // namespace locator
