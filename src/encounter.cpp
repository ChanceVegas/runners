// encounter.cpp — lane-chase gameplay. Obstacles live at fixed WORLD positions
// (wz, metres along the road); their depth on screen is z = wz - travel, so the
// only thing that moves each tick is travel. That keeps interpolation trivial.
// Placeholder art: everything is drawn with fillRect/fillEllipse (no bitmaps),
// so the unresolved sprite colour-key issue (CARRY-1) can't affect R1.
#include "encounter.h"
#include "lanes.h"
#include "input.h"
#include "config.h"
#include "board_config.h"
#include <Arduino.h>
#include <math.h>
#include <stdio.h>

namespace {

enum class Kind : uint8_t { Barrier, Wall };

struct Obstacle {
  bool  active;
  Kind  kind;
  int8_t lane;          // -1, 0, +1
  float wz;             // world position along the road, m
};

Obstacle s_obst[OBST_POOL];

encounter::State s_state = encounter::State::Ready;

// Logic state (tick resolution) + previous tick for render interpolation.
float s_travel = 0, s_prevTravel = 0;     // m
float s_laneX = 0, s_prevLaneX = 0;       // lanes, -1..1 (animated)
int8_t s_laneTarget = 0;
float s_jumpY = 0, s_prevJumpY = 0;       // px above the ground
float s_jumpV = 0;                        // px/s, + = up
float s_speed = 0;                        // m/s
float s_runStart = 0;                     // travel at the start of this attempt
float s_nextRowWz = 0;                    // world z of the next obstacle row
float s_endTimer = 0;                     // s since win/lose
float s_finalRun = 0;                     // m run when the attempt ended

// Interpolated values latched by beginRender.
float d_travel = 0, d_laneX = 0, d_jumpY = 0;

uint32_t s_rng = 1;
uint32_t rnd() { s_rng = s_rng * 1664525u + 1013904223u; return s_rng >> 8; }

// Runner's depth: the road depth at its feet row.
float playerZ() { return lanes::depthAtRow(RUN_FEET_Y); }

void clearObstacles() { for (auto& o : s_obst) o.active = false; }

void spawn(Kind k, int8_t lane, float wz) {
  for (auto& o : s_obst) {
    if (!o.active) { o = { true, k, lane, wz }; return; }
  }
}

// A row blocks 1 or 2 lanes, never all 3, so a dodge always exists.
void spawnRow(float wz) {
  int blocked = 1 + (int)(rnd() % 2);
  int free = (int)(rnd() % 3) - 1;                 // lane guaranteed open
  int placed = 0;
  for (int lane = -1; lane <= 1 && placed < blocked; ++lane) {
    if (lane == free) continue;
    if (blocked == 1 && (rnd() % 2)) continue;     // pick one of the other two
    Kind k = ((int)(rnd() % 100) < OBST_WALL_CHANCE) ? Kind::Wall : Kind::Barrier;
    spawn(k, (int8_t)lane, wz);
    ++placed;
  }
  if (placed == 0) {                               // blocked==1 skipped both: place one
    int lane = (free == 1) ? 0 : free + 1;
    spawn(Kind::Barrier, (int8_t)lane, wz);
  }
}

void startRun() {
  clearObstacles();
  s_runStart = s_travel;
  s_nextRowWz = s_travel + OBST_SPAWN_Z * 0.6f;    // first row a short way ahead
  s_laneTarget = 0;
  s_laneX = s_prevLaneX = 0;
  s_jumpY = s_prevJumpY = s_jumpV = 0;
  s_speed = RUN_SPEED_START;
  s_state = encounter::State::Run;
}

void endRun(encounter::State st) {
  s_state = st;
  s_endTimer = 0;
  s_finalRun = s_travel - s_runStart;
}

// --- drawing helpers ---------------------------------------------------------

inline bool rowsHit(int32_t y0, int32_t y1, int32_t bandY, int32_t bandH) {
  return y1 > bandY && y0 < bandY + bandH;
}

void rect(lgfx::LGFX_Sprite& b, int32_t bandY, int32_t x, int32_t y, int32_t w, int32_t h,
          uint16_t c) {
  if (w <= 0 || h <= 0 || !rowsHit(y, y + h, bandY, b.height())) return;
  b.fillRect(x, y - bandY, w, h, c);
}

void drawObstacle(lgfx::LGFX_Sprite& b, int32_t bandY, const Obstacle& o) {
  float sx, sy, s;
  if (!lanes::project(o.wz - d_travel, (float)o.lane, sx, sy, s)) return;
  if (s < 0.03f) return;                           // a speck at the horizon
  const int32_t w = (int32_t)(OBST_W_PX * s) + 1;
  const int32_t x = (int32_t)sx - w / 2;
  const int32_t yb = (int32_t)sy;
  if (o.kind == Kind::Barrier) {
    const int32_t h = (int32_t)(OBST_BARRIER_H_PX * s) + 1;
    const int32_t leg = w / 8 + 1;
    rect(b, bandY, x + leg, yb - h, leg, h, 0x4208);                 // legs
    rect(b, bandY, x + w - 2 * leg, yb - h, leg, h, 0x4208);
    rect(b, bandY, x, yb - h, w, h * 6 / 10 + 1, 0xFC00);            // orange board
    rect(b, bandY, x, yb - h + h / 5, w, h / 5 + 1, 0xFFFF);         // white stripe
  } else {
    const int32_t h = (int32_t)(OBST_WALL_H_PX * s) + 1;
    rect(b, bandY, x, yb - h, w, h, 0x31A6);                         // dark slab
    rect(b, bandY, x, yb - h, w, h / 10 + 1, 0x6B6D);                // lit top edge
    rect(b, bandY, x + w / 6, yb - h * 7 / 10, w * 2 / 3, h / 6 + 1, 0xFE60);  // warning band
  }
}

void drawRunner(lgfx::LGFX_Sprite& b, int32_t bandY) {
  float sx, sy, s;
  if (!lanes::project(playerZ(), d_laneX, sx, sy, s)) return;
  const int32_t cx = (int32_t)sx;
  const int32_t feet = RUN_FEET_Y - (int32_t)d_jumpY;

  // Shadow on the road: shrinks as the runner rises.
  const int32_t shw = 18 - (int32_t)(d_jumpY / 10.0f);
  if (shw > 4 && rowsHit(RUN_FEET_Y - 5, RUN_FEET_Y + 5, bandY, b.height()))
    b.fillEllipse(cx, RUN_FEET_Y - bandY, shw, 5, 0x2104);

  const bool air = d_jumpY > 1.0f;
  const bool stride = (((int32_t)(d_travel / 1.6f)) & 1) != 0;
  // Legs: alternate stride while running, tucked in the air.
  if (air) {
    rect(b, bandY, cx - 10, feet - 18, 9, 10, 0x2945);
    rect(b, bandY, cx + 1,  feet - 18, 9, 10, 0x2945);
  } else {
    rect(b, bandY, cx - 9, feet - 22, 7, stride ? 22 : 16, 0x2945);
    rect(b, bandY, cx + 2, feet - 22, 7, stride ? 16 : 22, 0x2945);
  }
  rect(b, bandY, cx - 13, feet - 50, 26, 30, 0x05E0);   // green jersey
  rect(b, bandY, cx - 13, feet - 50, 26, 4, 0x07E0);    // collar highlight
  rect(b, bandY, cx - 18, feet - 46, 5, 18, 0xF6B2);    // arms
  rect(b, bandY, cx + 13, feet - 46, 5, 18, 0xF6B2);
  rect(b, bandY, cx - 9, feet - 66, 18, 16, 0xF6B2);    // head (seen from behind)
  rect(b, bandY, cx - 10, feet - 70, 20, 8, 0x8200);    // hair
}

void text(lgfx::LGFX_Sprite& b, int32_t bandY, const char* str, int32_t x, int32_t y,
          int size, uint16_t color) {
  const int32_t half = 4 * size + 2;                    // default font is 8 px tall
  if (!rowsHit(y - half, y + half, bandY, b.height())) return;
  b.setTextSize(size);
  b.setTextDatum(lgfx::middle_center);
  b.setTextColor(0x0000);                               // 2 px drop shadow
  b.drawString(str, x + 2, y - bandY + 2);
  b.setTextColor(color);
  b.drawString(str, x, y - bandY);
}

} // namespace

namespace encounter {

bool init() {
  s_rng = esp_random() | 1u;
  clearObstacles();
  s_state = State::Ready;
  s_travel = s_prevTravel = 0;
  return true;
}

void update(float dt) {
  const input::State& in = input::state();
  s_prevTravel = s_travel;
  s_prevLaneX = s_laneX;
  s_prevJumpY = s_jumpY;

  switch (s_state) {
    case State::Ready:
      s_travel += RUN_ATTRACT_SPEED * dt;
      if (in.pressed) startRun();
      return;

    case State::Win:
      s_travel += RUN_ATTRACT_SPEED * dt;
      // fallthrough
    case State::Lose:
      s_endTimer += dt;
      if (s_endTimer > RUN_END_LOCKOUT_S && in.pressed) startRun();
      return;

    case State::Run:
      break;
  }

  // Controls.
  if (in.laneStep < 0 && s_laneTarget > -1) --s_laneTarget;
  if (in.laneStep > 0 && s_laneTarget <  1) ++s_laneTarget;
  const bool jumpOk = in.jumpPressed && s_jumpY <= 0.0f;
  if (jumpOk) s_jumpV = RUN_JUMP_VEL_PX_S;
#if DEBUG_INPUT_LOG
  if (in.laneStep) Serial.printf("[in] tap %s -> lane %d at %d m\n",
                                 in.laneStep < 0 ? "LEFT" : "RIGHT", s_laneTarget,
                                 (int)(s_travel - s_runStart));
  if (in.jumpPressed) Serial.printf("[in] tap MIDDLE: jump%s at %d m\n",
                                    jumpOk ? "" : " (ignored: airborne)",
                                    (int)(s_travel - s_runStart));
#endif

  // Sideways glide toward the target lane.
  const float step = RUN_LANE_SPEED * dt;
  if (s_laneX < s_laneTarget) s_laneX = fminf(s_laneX + step, (float)s_laneTarget);
  else if (s_laneX > s_laneTarget) s_laneX = fmaxf(s_laneX - step, (float)s_laneTarget);

  // Jump arc.
  if (s_jumpY > 0.0f || s_jumpV > 0.0f) {
    s_jumpV -= RUN_GRAVITY_PX_S2 * dt;
    s_jumpY += s_jumpV * dt;
    if (s_jumpY <= 0.0f) { s_jumpY = 0.0f; s_jumpV = 0.0f; }
  }

  // Forward motion and difficulty.
  s_speed = fminf(s_speed + RUN_SPEED_RAMP * dt, RUN_SPEED_MAX);
  s_travel += s_speed * dt;
  const float run = s_travel - s_runStart;
  const float prog = run / RUN_GOAL_M;

  // Spawn rows ahead, stopping short of the finish.
  while (s_nextRowWz < s_travel + OBST_SPAWN_Z && s_nextRowWz < s_runStart + RUN_GOAL_M - 20.0f) {
    spawnRow(s_nextRowWz);
    s_nextRowWz += OBST_GAP_START_M + (OBST_GAP_MIN_M - OBST_GAP_START_M) * fminf(prog, 1.0f);
  }

  // Collision + recycling.
  const float pz = playerZ();
  for (auto& o : s_obst) {
    if (!o.active) continue;
    const float z = o.wz - s_travel;
    if (z < 0.5f) { o.active = false; continue; }   // passed and off-screen
    if (fabsf(z - pz) < OBST_HIT_DEPTH_M && fabsf(s_laneX - o.lane) < OBST_HIT_LANE) {
      if (o.kind == Kind::Wall || s_jumpY < OBST_CLEAR_PX) {
#if DEBUG_INPUT_LOG
        Serial.printf("[hit] %s lane %d, runner lane %.2f jumpY %d at %d m\n",
                      o.kind == Kind::Wall ? "WALL" : "BARRIER", o.lane, s_laneX,
                      (int)s_jumpY, (int)(s_travel - s_runStart));
#endif
        endRun(State::Lose);
        return;
      }
    }
  }

  if (run >= RUN_GOAL_M) endRun(State::Win);
}

void beginRender(float alpha) {
  if (alpha < 0) alpha = 0;
  if (alpha > 1) alpha = 1;
  // Freeze in place on a crash: interpolating would replay the last partial tick.
  const float a = (s_state == State::Lose) ? 1.0f : alpha;
  d_travel = s_prevTravel + (s_travel - s_prevTravel) * a;
  d_laneX  = s_prevLaneX + (s_laneX - s_prevLaneX) * a;
  d_jumpY  = s_prevJumpY + (s_jumpY - s_prevJumpY) * a;
  lanes::beginFrame(d_travel);
}

void composeObjects(lgfx::LGFX_Sprite& band, int32_t bandY) {
  // Painter's order: far to near. Small pool, so a simple index sort is fine.
  int8_t order[OBST_POOL];
  int n = 0;
  for (int i = 0; i < OBST_POOL; ++i) if (s_obst[i].active) order[n++] = (int8_t)i;
  for (int i = 1; i < n; ++i) {
    int8_t k = order[i];
    int j = i - 1;
    while (j >= 0 && s_obst[order[j]].wz < s_obst[k].wz) { order[j + 1] = order[j]; --j; }
    order[j + 1] = k;
  }

  const float pz = playerZ();
  bool runnerDrawn = (s_state == State::Ready);   // no runner on the title screen
  for (int i = 0; i < n; ++i) {
    const Obstacle& o = s_obst[order[i]];
    if (!runnerDrawn && o.wz - d_travel < pz) { drawRunner(band, bandY); runnerDrawn = true; }
    drawObstacle(band, bandY, o);
  }
  if (!runnerDrawn) drawRunner(band, bandY);
}

void composeHud(lgfx::LGFX_Sprite& band, int32_t bandY) {
  char buf[32];
  const float run = (s_state == State::Run) ? s_travel - s_runStart : s_finalRun;

  if (s_state == State::Run || s_state == State::Lose || s_state == State::Win) {
    // Escape progress bar along the top.
    const float p = fminf(run / RUN_GOAL_M, 1.0f);
    rect(band, bandY, 10, 8, LCD_WIDTH - 20, 10, 0x2104);
    rect(band, bandY, 12, 10, (int32_t)((LCD_WIDTH - 24) * p), 6, 0x07E0);
    snprintf(buf, sizeof buf, "%d m", (int)run);
    text(band, bandY, buf, 60, 34, 2, 0xFFFF);
    snprintf(buf, sizeof buf, "%d km/h", (int)(s_speed * 3.6f));
    text(band, bandY, buf, LCD_WIDTH - 70, 34, 2, 0xFFFF);
  }

  // Tap-zone hints along the bottom: where to tap for left / jump / right.
  if (s_state == State::Run || s_state == State::Ready) {
    rect(band, bandY, INPUT_ZONE_LEFT_X, LCD_HEIGHT - 14, 1, 14, 0x8410);
    rect(band, bandY, INPUT_ZONE_RIGHT_X, LCD_HEIGHT - 14, 1, 14, 0x8410);
    text(band, bandY, "<", INPUT_ZONE_LEFT_X / 2, LCD_HEIGHT - 10, 2, 0xC618);
    text(band, bandY, "JUMP", LCD_WIDTH / 2, LCD_HEIGHT - 10, 1, 0xC618);
    text(band, bandY, ">", (INPUT_ZONE_RIGHT_X + LCD_WIDTH) / 2, LCD_HEIGHT - 10, 2, 0xC618);
  }

  switch (s_state) {
    case State::Ready:
      text(band, bandY, "RUNNERS", LCD_WIDTH / 2, 70, 5, 0xFFE0);
      text(band, bandY, "tap to run", LCD_WIDTH / 2, 150, 2, 0xFFFF);
      text(band, bandY, "tap LEFT / RIGHT side = change lane", LCD_WIDTH / 2, 178, 1, 0xFFFF);
      text(band, bandY, "tap MIDDLE = jump", LCD_WIDTH / 2, 192, 1, 0xFFFF);
      break;
    case State::Win:
      text(band, bandY, "ESCAPED!", LCD_WIDTH / 2, 110, 4, 0x07E0);
      if (s_endTimer > RUN_END_LOCKOUT_S) text(band, bandY, "tap to run again", LCD_WIDTH / 2, 160, 2, 0xFFFF);
      break;
    case State::Lose:
      text(band, bandY, "CAUGHT!", LCD_WIDTH / 2, 110, 4, 0xF800);
      if (s_endTimer > RUN_END_LOCKOUT_S) text(band, bandY, "tap to retry", LCD_WIDTH / 2, 160, 2, 0xFFFF);
      break;
    case State::Run:
      break;
  }
}

State state()    { return s_state; }
float distanceM() { return (s_state == State::Run) ? s_travel - s_runStart : s_finalRun; }
float speedMS()  { return s_speed; }

} // namespace encounter
