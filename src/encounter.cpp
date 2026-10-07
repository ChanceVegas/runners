// encounter.cpp — lane-chase gameplay. Obstacles and coins live at fixed WORLD
// positions (wz, metres along the road); their depth on screen is z = wz - travel,
// so the only thing that moves each tick is travel. That keeps interpolation trivial.
//
// Flow: Ready (title, best score) -> tap -> Countdown -> Run -> hit -> Crash ->
// GameOver -> tap -> Countdown (stage 1). Reaching RUN_GOAL_M -> StageClear -> tap
// -> Countdown (next stage: faster, more walls). Score = metres across all stages
// + coins x COIN_POINTS; best is saved to flash (NVS) at game over.
//
// Art is drawn with fillRect/fillEllipse/fillTriangle (no bitmaps), so the open
// sprite colour-key issue (CARRY-1) can't affect it. Text uses LovyanGFX's smooth
// FreeSansBold fonts.
#include "encounter.h"
#include "lanes.h"
#include "scenery.h"
#include "input.h"
#include "config.h"
#include "color.h"
#include "board_config.h"
#include <Arduino.h>
#include <Preferences.h>
#include <math.h>
#include <stdio.h>

namespace {

using encounter::State;

// ---- World objects ------------------------------------------------------------
enum class Kind : uint8_t { Barrier, Wall };

struct Obstacle { bool active; Kind kind; int8_t lane; float wz; };
struct Coin     { bool active; bool high; int8_t lane; float wz; };

Obstacle s_obst[OBST_POOL];
Coin     s_coin[COIN_POOL];

// ---- Game state ---------------------------------------------------------------
State s_state = State::Ready;
float s_stateT = 0;                        // s in the current state

float s_travel = 0, s_prevTravel = 0;      // m
float s_laneX = 0, s_prevLaneX = 0;        // lanes, -1..1 (animated)
int8_t s_laneTarget = 0;
float s_jumpY = 0, s_prevJumpY = 0;        // px above the ground
float s_jumpV = 0;                         // px/s, + = up
float s_speed = 0;                         // m/s
float s_speedMax = RUN_SPEED_MAX;          // this stage's cap
int   s_wallChance = OBST_WALL_CHANCE;     // this stage's wall %
float s_runStart = 0;                      // travel when this stage started
float s_nextRowWz = 0;
float s_finalRun = 0;                      // m run when the stage ended

uint8_t  s_stage = 1;
uint32_t s_coins = 0;                      // coins this session
uint32_t s_bankedM = 0;                    // metres from cleared stages
uint32_t s_best = 0;
bool     s_newBest = false;
Preferences s_prefs;

// ---- Render-side (latched in beginRender) ---------------------------------------
float d_travel = 0, d_laneX = 0, d_jumpY = 0, d_time = 0;

// One depth-sorted list of everything on the road, built once per frame.
enum class DrawKind : uint8_t { Obstacle, Coin, Runner };
struct DrawItem { DrawKind kind; uint8_t idx; int16_t x, y; float s; float z; };
DrawItem s_draw[OBST_POOL + COIN_POOL + 1];
int s_nDraw = 0;

uint32_t s_rng = 1;
uint32_t rnd() { s_rng = s_rng * 1664525u + 1013904223u; return s_rng >> 8; }

float playerZ() { return lanes::depthAtRow(RUN_FEET_Y); }

float runM() {
  return (s_state == State::Run) ? s_travel - s_runStart : s_finalRun;
}

// Metres of the current stage not yet banked: live while running, frozen after a
// crash, zero once banked (stage clear) or before running (countdown).
uint32_t currentScore() {
  float m = 0.0f;
  if (s_state == State::Run) m = s_travel - s_runStart;
  else if (s_state == State::Crash || s_state == State::GameOver) m = s_finalRun;
  return s_bankedM + (uint32_t)m + s_coins * COIN_POINTS;
}

void clearWorld() {
  for (auto& o : s_obst) o.active = false;
  for (auto& c : s_coin) c.active = false;
}

void spawnObstacle(Kind k, int8_t lane, float wz) {
  for (auto& o : s_obst) if (!o.active) { o = { true, k, lane, wz }; return; }
}

void spawnCoin(bool high, int8_t lane, float wz) {
  for (auto& c : s_coin) if (!c.active) { c = { true, high, lane, wz }; return; }
}

// A row blocks 1 or 2 lanes, never all 3, so a dodge always exists. Sometimes a coin
// trail leads into the open lane, or arcs over a barrier (only reachable by jumping).
void spawnRow(float wz) {
  const int blocked = 1 + (int)(rnd() % 2);
  const int open = (int)(rnd() % 3) - 1;
  int placed = 0;
  int barrierLane = -2;
  for (int lane = -1; lane <= 1 && placed < blocked; ++lane) {
    if (lane == open) continue;
    if (blocked == 1 && (rnd() % 2)) continue;
    const Kind k = ((int)(rnd() % 100) < s_wallChance) ? Kind::Wall : Kind::Barrier;
    spawnObstacle(k, (int8_t)lane, wz);
    if (k == Kind::Barrier) barrierLane = lane;
    ++placed;
  }
  if (placed == 0) {
    const int lane = (open == 1) ? 0 : open + 1;
    spawnObstacle(Kind::Barrier, (int8_t)lane, wz);
    barrierLane = lane;
  }

  if ((int)(rnd() % 100) >= COIN_TRAIL_CHANCE) return;
  if (barrierLane != -2 && (int)(rnd() % 100) < COIN_ARC_CHANCE) {
    for (int i = -1; i <= 1; ++i)                       // arc over the barrier
      spawnCoin(true, (int8_t)barrierLane, wz + i * 2.0f);
  } else {
    for (int i = 0; i < COIN_TRAIL_N; ++i)              // trail leading into the gap
      spawnCoin(false, (int8_t)open, wz - (COIN_TRAIL_N - i) * COIN_TRAIL_STEP_M);
  }
}

void setState(State st) { s_state = st; s_stateT = 0; }

void beginStage() {
  clearWorld();
  const float step = (s_stage - 1) * STAGE_SPEED_STEP;
  s_speed = fminf(RUN_SPEED_START + step, STAGE_SPEED_CAP);
  s_speedMax = fminf(RUN_SPEED_MAX + step, STAGE_SPEED_CAP);
  s_wallChance = OBST_WALL_CHANCE + (s_stage - 1) * STAGE_WALL_STEP;
  if (s_wallChance > STAGE_WALL_CAP) s_wallChance = STAGE_WALL_CAP;
  s_laneTarget = 0;
  s_laneX = s_prevLaneX = 0;
  s_jumpY = s_prevJumpY = s_jumpV = 0;
  setState(State::Countdown);
}

void newGame() {
  s_stage = 1;
  s_coins = 0;
  s_bankedM = 0;
  s_newBest = false;
  beginStage();
}

void startRunning() {
  s_runStart = s_travel;
  s_nextRowWz = s_travel + RUN_FIRST_ROW_M;
  setState(State::Run);
}

void crash() {
  s_finalRun = s_travel - s_runStart;
  setState(State::Crash);
}

void gameOver() {
  const uint32_t sc = currentScore();
  if (sc > s_best) {
    s_best = sc;
    s_newBest = true;
    s_prefs.putUInt("best", s_best);      // rare flash write: once per game over
  }
  setState(State::GameOver);
}

void stageClear() {
  s_finalRun = s_travel - s_runStart;
  s_bankedM += (uint32_t)s_finalRun;
  clearWorld();
  setState(State::StageClear);
}

// ---- Drawing helpers -------------------------------------------------------------

inline bool rowsHit(int32_t y0, int32_t y1, int32_t bandY, int32_t bandH) {
  return y1 > bandY && y0 < bandY + bandH;
}

void rect(lgfx::LGFX_Sprite& b, int32_t bandY, int32_t x, int32_t y, int32_t w, int32_t h,
          uint16_t c) {
  if (w <= 0 || h <= 0 || !rowsHit(y, y + h, bandY, b.height())) return;
  b.fillRect(x, y - bandY, w, h, c);
}

// Centred text with a drop shadow, auto-shrunk to fit maxW. Skips bands it misses.
void text(lgfx::LGFX_Sprite& b, int32_t bandY, const char* str, int32_t x, int32_t y,
          const lgfx::IFont* font, float size, uint16_t color, int32_t maxW = LCD_WIDTH - 16) {
  b.setFont(font);
  b.setTextSize(size);
  const int32_t w = b.textWidth(str);
  if (w > maxW && w > 0) { size *= (float)maxW / w; b.setTextSize(size); }
  const int32_t half = b.fontHeight() / 2 + 3;
  if (!rowsHit(y - half, y + half, bandY, b.height())) return;
  b.setTextDatum(lgfx::middle_center);
  b.setTextColor(rgb565(0, 0, 0));
  b.drawString(str, x + 2, y - bandY + 2);
  b.setTextColor(color);
  b.drawString(str, x, y - bandY);
}

constexpr uint16_t C_WHITE  = rgb565(255, 255, 255);
constexpr uint16_t C_YELLOW = rgb565(255, 220, 40);
constexpr uint16_t C_GOLD   = rgb565(240, 180, 20);
constexpr uint16_t C_GOLD_HI= rgb565(255, 240, 150);
constexpr uint16_t C_GREEN  = rgb565(60, 220, 90);
constexpr uint16_t C_RED    = rgb565(235, 50, 40);
constexpr uint16_t C_GREY   = rgb565(200, 200, 200);
constexpr uint16_t C_SHADOW = rgb565(32, 32, 36);

void drawObstacle(lgfx::LGFX_Sprite& b, int32_t bandY, const DrawItem& d) {
  const Obstacle& o = s_obst[d.idx];
  const float s = d.s;
  const int32_t w = (int32_t)(OBST_W_PX * s) + 1;
  const int32_t x = d.x - w / 2;
  const int32_t yb = d.y;
  if (o.kind == Kind::Barrier) {
    const int32_t h = (int32_t)(OBST_BARRIER_H_PX * s) + 1;
    const int32_t leg = w / 8 + 1;
    rect(b, bandY, x + leg, yb - h, leg, h, rgb565(70, 70, 70));                 // legs
    rect(b, bandY, x + w - 2 * leg, yb - h, leg, h, rgb565(70, 70, 70));
    rect(b, bandY, x, yb - h, w, h * 6 / 10 + 1, rgb565(250, 130, 0));          // board
    rect(b, bandY, x, yb - h + h / 5, w, h / 5 + 1, C_WHITE);                    // stripe
    rect(b, bandY, x, yb - h, w, h / 14 + 1, rgb565(255, 190, 90));              // lit edge
  } else {
    const int32_t h = (int32_t)(OBST_WALL_H_PX * s) + 1;
    rect(b, bandY, x, yb - h, w, h, rgb565(50, 54, 62));                         // slab
    rect(b, bandY, x, yb - h, w, h / 10 + 1, rgb565(108, 112, 122));             // top
    rect(b, bandY, x + w - w / 8, yb - h, w / 8 + 1, h, rgb565(36, 38, 44));     // side shade
    rect(b, bandY, x + w / 6, yb - h * 7 / 10, w * 2 / 3, h / 6 + 1, rgb565(255, 200, 0));
    rect(b, bandY, x + w / 6, yb - h * 7 / 10 + h / 12, w * 2 / 3, h / 24 + 1, rgb565(30, 30, 30));
  }
}

void drawCoin(lgfx::LGFX_Sprite& b, int32_t bandY, const DrawItem& d) {
  const Coin& c = s_coin[d.idx];
  const float s = d.s;
  const int32_t r = (int32_t)(COIN_R_PX * s) + 1;
  const int32_t yc = d.y - (int32_t)((c.high ? COIN_HIGH_PX : COIN_LOW_PX) * s);
  if (!rowsHit(yc - r, yc + r, bandY, b.height())) return;
  // Spin: horizontal radius follows |cos|, phase offset per coin so they don't sync.
  const float spin = fabsf(cosf(d_time * 6.0f + c.wz * 0.7f));
  const int32_t rx = (int32_t)(r * (0.25f + 0.75f * spin)) + 1;
  b.fillEllipse(d.x, yc - bandY, rx, r, C_GOLD);
  if (rx > 2) b.fillEllipse(d.x - rx / 4, yc - r / 4 - bandY, rx / 2, r / 2, C_GOLD_HI);
}

void drawRunner(lgfx::LGFX_Sprite& b, int32_t bandY, const DrawItem& d) {
  const int32_t cx = d.x;
  const bool down = (s_state == State::Crash || s_state == State::GameOver);
  const int32_t feet = RUN_FEET_Y - (int32_t)d_jumpY;

  const int32_t shw = 20 - (int32_t)(d_jumpY / 9.0f);
  if (shw > 4 && rowsHit(RUN_FEET_Y - 6, RUN_FEET_Y + 6, bandY, b.height()))
    b.fillEllipse(cx, RUN_FEET_Y - bandY, shw, 6, C_SHADOW);

  const uint16_t JERSEY = rgb565(30, 170, 80), JERSEY_HI = rgb565(90, 225, 130);
  const uint16_t SKIN = rgb565(240, 196, 150), HAIR = rgb565(110, 60, 20);
  const uint16_t SHORTS = rgb565(40, 50, 90), SHOE = rgb565(240, 240, 240);

  if (down) {                                           // knocked flat
    rect(b, bandY, cx - 34, RUN_FEET_Y - 16, 50, 14, JERSEY);
    rect(b, bandY, cx + 16, RUN_FEET_Y - 18, 16, 16, SKIN);
    rect(b, bandY, cx + 28, RUN_FEET_Y - 18, 6, 16, HAIR);
    rect(b, bandY, cx - 48, RUN_FEET_Y - 12, 16, 8, SHORTS);
    return;
  }

  // Lean into a lane change: offset the upper body toward the target lane.
  const float lean = (float)s_laneTarget - d_laneX;
  const int32_t lx = (int32_t)(lean * 10.0f);
  const bool air = d_jumpY > 1.0f;
  const bool stride = (((int32_t)(d_travel / 1.6f)) & 1) != 0;
  const bool moving = (s_state == State::Run || s_state == State::StageClear ||
                       s_state == State::Countdown);

  if (air) {                                            // tucked legs
    rect(b, bandY, cx - 11, feet - 20, 10, 12, SHORTS);
    rect(b, bandY, cx + 1,  feet - 20, 10, 12, SHORTS);
    rect(b, bandY, cx - 11, feet - 10, 10, 5, SHOE);
    rect(b, bandY, cx + 1,  feet - 10, 10, 5, SHOE);
  } else {
    const int32_t lA = (moving && stride) ? 24 : 18, lB = (moving && !stride) ? 24 : 18;
    rect(b, bandY, cx - 10, feet - 24, 8, lA - 4, SKIN);
    rect(b, bandY, cx + 2,  feet - 24, 8, lB - 4, SKIN);
    rect(b, bandY, cx - 11, feet - 24 + lA - 5, 10, 5, SHOE);
    rect(b, bandY, cx + 1,  feet - 24 + lB - 5, 10, 5, SHOE);
    rect(b, bandY, cx - 12, feet - 30, 24, 8, SHORTS);
  }
  rect(b, bandY, cx - 14 + lx / 2, feet - 56, 28, 28, JERSEY);
  rect(b, bandY, cx - 14 + lx / 2, feet - 56, 28, 5, JERSEY_HI);
  rect(b, bandY, cx - 6 + lx / 2,  feet - 48, 12, 12, C_WHITE);    // race number bib
  rect(b, bandY, cx - 20 + lx / 2, feet - 52, 6, 18, SKIN);         // arms
  rect(b, bandY, cx + 14 + lx / 2, feet - 52, 6, 18, SKIN);
  rect(b, bandY, cx - 9 + lx, feet - 72, 18, 16, SKIN);             // head (from behind)
  rect(b, bandY, cx - 10 + lx, feet - 76, 20, 9, HAIR);
}

void drawCoinIcon(lgfx::LGFX_Sprite& b, int32_t bandY, int32_t x, int32_t y) {
  if (!rowsHit(y - 9, y + 9, bandY, b.height())) return;
  b.fillEllipse(x, y - bandY, 8, 9, C_GOLD);
  b.fillEllipse(x - 2, y - 3 - bandY, 4, 4, C_GOLD_HI);
}

} // namespace

namespace encounter {

bool init() {
  s_rng = esp_random() | 1u;
  clearWorld();
  s_prefs.begin("runners", false);
  s_best = s_prefs.getUInt("best", 0);
  Serial.printf("[encounter] best score loaded: %u\n", (unsigned)s_best);
  s_travel = s_prevTravel = 0;
  setState(State::Ready);
  return true;
}

void update(float dt) {
  const input::State& in = input::state();
  s_prevTravel = s_travel;
  s_prevLaneX = s_laneX;
  s_prevJumpY = s_jumpY;
  s_stateT += dt;

  switch (s_state) {
    case State::Ready:
      s_travel += RUN_ATTRACT_SPEED * dt;
      if (in.pressed) newGame();
      return;

    case State::Countdown:
      s_travel += RUN_ATTRACT_SPEED * dt;
      if (s_stateT >= RUN_COUNTDOWN_S) startRunning();
      return;

    case State::Crash:
      if (s_stateT >= RUN_CRASH_S) gameOver();
      return;

    case State::StageClear:
      s_travel += RUN_ATTRACT_SPEED * dt;
      if (s_stateT > RUN_END_LOCKOUT_S && in.pressed) { ++s_stage; beginStage(); }
      return;

    case State::GameOver:
      if (s_stateT > RUN_END_LOCKOUT_S && in.pressed) newGame();
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

  const float step = RUN_LANE_SPEED * dt;
  if (s_laneX < s_laneTarget) s_laneX = fminf(s_laneX + step, (float)s_laneTarget);
  else if (s_laneX > s_laneTarget) s_laneX = fmaxf(s_laneX - step, (float)s_laneTarget);

  if (s_jumpY > 0.0f || s_jumpV > 0.0f) {
    s_jumpV -= RUN_GRAVITY_PX_S2 * dt;
    s_jumpY += s_jumpV * dt;
    if (s_jumpY <= 0.0f) { s_jumpY = 0.0f; s_jumpV = 0.0f; }
  }

  s_speed = fminf(s_speed + RUN_SPEED_RAMP * dt, s_speedMax);
  s_travel += s_speed * dt;
  const float run = s_travel - s_runStart;
  const float prog = run / RUN_GOAL_M;

  while (s_nextRowWz < s_travel + OBST_SPAWN_Z && s_nextRowWz < s_runStart + RUN_GOAL_M - 20.0f) {
    spawnRow(s_nextRowWz);
    s_nextRowWz += OBST_GAP_START_M + (OBST_GAP_MIN_M - OBST_GAP_START_M) * fminf(prog, 1.0f);
  }

  const float pz = playerZ();

  // Coins: collect or recycle.
  for (auto& c : s_coin) {
    if (!c.active) continue;
    const float z = c.wz - s_travel;
    if (z < 0.5f) { c.active = false; continue; }
    if (fabsf(z - pz) < COIN_HIT_DEPTH_M && fabsf(s_laneX - c.lane) < 0.5f) {
      const bool reach = c.high ? (s_jumpY >= COIN_HIGH_MIN_JUMP) : (s_jumpY <= COIN_LOW_MAX_JUMP);
      if (reach) { c.active = false; ++s_coins; }
    }
  }

  // Obstacles: collide or recycle.
  for (auto& o : s_obst) {
    if (!o.active) continue;
    const float z = o.wz - s_travel;
    if (z < 0.5f) { o.active = false; continue; }
    if (fabsf(z - pz) < OBST_HIT_DEPTH_M && fabsf(s_laneX - o.lane) < OBST_HIT_LANE) {
      if (o.kind == Kind::Wall || s_jumpY < OBST_CLEAR_PX) {
#if DEBUG_INPUT_LOG
        Serial.printf("[hit] %s lane %d, runner lane %.2f jumpY %d at %d m\n",
                      o.kind == Kind::Wall ? "WALL" : "BARRIER", o.lane, s_laneX,
                      (int)s_jumpY, (int)run);
#endif
        crash();
        return;
      }
    }
  }

  if (run >= RUN_GOAL_M) stageClear();
}

void beginRender(float alpha) {
  if (alpha < 0) alpha = 0;
  if (alpha > 1) alpha = 1;
  const bool frozen = (s_state == State::Crash || s_state == State::GameOver);
  const float a = frozen ? 1.0f : alpha;
  d_travel = s_prevTravel + (s_travel - s_prevTravel) * a;
  d_laneX  = s_prevLaneX + (s_laneX - s_prevLaneX) * a;
  d_jumpY  = s_prevJumpY + (s_jumpY - s_prevJumpY) * a;
  d_time   = millis() * 0.001f;
  lanes::beginFrame(d_travel);
  scenery::beginFrame(d_travel);

  // Project everything once, then sort far -> near (insertion sort: small, mostly
  // ordered already because spawns arrive in depth order).
  s_nDraw = 0;
  float sx, sy, s;
  for (int i = 0; i < OBST_POOL; ++i) {
    const Obstacle& o = s_obst[i];
    if (!o.active) continue;
    const float z = o.wz - d_travel;
    if (!lanes::project(z, (float)o.lane, sx, sy, s) || s < 0.03f) continue;
    s_draw[s_nDraw++] = { DrawKind::Obstacle, (uint8_t)i, (int16_t)sx, (int16_t)sy, s, z };
  }
  for (int i = 0; i < COIN_POOL; ++i) {
    const Coin& c = s_coin[i];
    if (!c.active) continue;
    const float z = c.wz - d_travel;
    if (!lanes::project(z, (float)c.lane, sx, sy, s) || s < 0.03f) continue;
    s_draw[s_nDraw++] = { DrawKind::Coin, (uint8_t)i, (int16_t)sx, (int16_t)sy, s, z };
  }
  if (s_state != State::Ready) {
    const float pz = playerZ();
    if (lanes::project(pz, d_laneX, sx, sy, s))
      s_draw[s_nDraw++] = { DrawKind::Runner, 0, (int16_t)sx, (int16_t)sy, s, pz };
  }
  for (int i = 1; i < s_nDraw; ++i) {
    DrawItem k = s_draw[i];
    int j = i - 1;
    while (j >= 0 && s_draw[j].z < k.z) { s_draw[j + 1] = s_draw[j]; --j; }
    s_draw[j + 1] = k;
  }
}

void composeObjects(lgfx::LGFX_Sprite& band, int32_t bandY) {
  for (int i = 0; i < s_nDraw; ++i) {
    const DrawItem& d = s_draw[i];
    switch (d.kind) {
      case DrawKind::Obstacle: drawObstacle(band, bandY, d); break;
      case DrawKind::Coin:     drawCoin(band, bandY, d);     break;
      case DrawKind::Runner:   drawRunner(band, bandY, d);   break;
    }
  }
}

void composeHud(lgfx::LGFX_Sprite& band, int32_t bandY) {
  char buf[40];
  const lgfx::IFont* F9  = &fonts::FreeSansBold9pt7b;
  const lgfx::IFont* F12 = &fonts::FreeSansBold12pt7b;
  const lgfx::IFont* F18 = &fonts::FreeSansBold18pt7b;
  const lgfx::IFont* F24 = &fonts::FreeSansBold24pt7b;
  const int32_t cx = LCD_WIDTH / 2;
  const bool blink = ((millis() / 450) & 1) == 0;

  // In-game HUD: escape progress bar, score, stage, coins.
  if (s_state == State::Run || s_state == State::Countdown || s_state == State::Crash) {
    const float p = fminf(runM() / RUN_GOAL_M, 1.0f);
    rect(band, bandY, 10, 6, LCD_WIDTH - 20, 10, C_SHADOW);
    rect(band, bandY, 12, 8, (int32_t)((LCD_WIDTH - 24) * (s_state == State::Countdown ? 0 : p)),
         6, C_GREEN);
    snprintf(buf, sizeof buf, "%u", (unsigned)currentScore());
    text(band, bandY, buf, 70, 34, F12, 1.0f, C_WHITE, 130);
    snprintf(buf, sizeof buf, "STAGE %u", (unsigned)s_stage);
    text(band, bandY, buf, cx, 34, F9, 1.0f, C_GREY);
    drawCoinIcon(band, bandY, LCD_WIDTH - 90, 34);
    snprintf(buf, sizeof buf, "%u", (unsigned)s_coins);
    text(band, bandY, buf, LCD_WIDTH - 50, 34, F12, 1.0f, C_YELLOW, 70);
  }

  // Tap-zone hints: on the title, through the countdown and the first seconds of a run.
  const bool hints = s_state == State::Ready || s_state == State::Countdown ||
                     (s_state == State::Run && s_stateT < RUN_HINT_S);
  if (hints) {
    rect(band, bandY, INPUT_ZONE_LEFT_X, LCD_HEIGHT - 30, 2, 30, C_GREY);
    rect(band, bandY, INPUT_ZONE_RIGHT_X, LCD_HEIGHT - 30, 2, 30, C_GREY);
    text(band, bandY, "< LANE", INPUT_ZONE_LEFT_X / 2, LCD_HEIGHT - 15, F12, 1.0f, C_WHITE, 150);
    text(band, bandY, "JUMP", cx, LCD_HEIGHT - 15, F12, 1.0f, C_WHITE, 150);
    text(band, bandY, "LANE >", (INPUT_ZONE_RIGHT_X + LCD_WIDTH) / 2, LCD_HEIGHT - 15, F12, 1.0f,
         C_WHITE, 150);
  }

  switch (s_state) {
    case State::Ready:
      text(band, bandY, "RUNNERS", cx, 52, F24, 1.4f, C_YELLOW);
      snprintf(buf, sizeof buf, "BEST  %u", (unsigned)s_best);
      text(band, bandY, buf, cx, 104, F12, 1.0f, C_WHITE);
      if (blink) text(band, bandY, "TAP TO RUN", cx, 146, F18, 1.0f, C_WHITE);
      text(band, bandY, "Tap LEFT or RIGHT side to change lane", cx, 188, F12, 1.0f, C_WHITE);
      text(band, bandY, "Tap the MIDDLE to jump", cx, 214, F12, 1.0f, C_WHITE);
      break;

    case State::Countdown: {
      const int n = 3 - (int)(s_stateT / (RUN_COUNTDOWN_S / 4.0f));
      if (n >= 1) { snprintf(buf, sizeof buf, "%d", n); text(band, bandY, buf, cx, 120, F24, 2.0f, C_YELLOW); }
      else text(band, bandY, "GO!", cx, 120, F24, 1.8f, C_GREEN);
      if (s_stage > 1) {
        snprintf(buf, sizeof buf, "STAGE %u", (unsigned)s_stage);
        text(band, bandY, buf, cx, 176, F18, 1.0f, C_WHITE);
      }
      break;
    }

    case State::Crash:
      if (s_stateT < RUN_CRASH_FLASH_S) {               // red impact border
        rect(band, bandY, 0, 0, LCD_WIDTH, 10, C_RED);
        rect(band, bandY, 0, LCD_HEIGHT - 10, LCD_WIDTH, 10, C_RED);
        rect(band, bandY, 0, 0, 10, LCD_HEIGHT, C_RED);
        rect(band, bandY, LCD_WIDTH - 10, 0, 10, LCD_HEIGHT, C_RED);
      }
      break;

    case State::StageClear:
      text(band, bandY, "ESCAPED!", cx, 70, F24, 1.3f, C_GREEN);
      snprintf(buf, sizeof buf, "STAGE %u CLEAR", (unsigned)s_stage);
      text(band, bandY, buf, cx, 122, F18, 1.0f, C_WHITE);
      snprintf(buf, sizeof buf, "SCORE %u", (unsigned)currentScore());
      text(band, bandY, buf, cx, 160, F12, 1.0f, C_YELLOW);
      if (s_stateT > RUN_END_LOCKOUT_S && blink)
        text(band, bandY, "TAP FOR NEXT STAGE", cx, 210, F12, 1.0f, C_WHITE);
      break;

    case State::GameOver:
      text(band, bandY, "CAUGHT!", cx, 62, F24, 1.3f, C_RED);
      snprintf(buf, sizeof buf, "SCORE %u", (unsigned)currentScore());
      text(band, bandY, buf, cx, 114, F18, 1.0f, C_WHITE);
      if (s_newBest) {
        if (blink) text(band, bandY, "NEW BEST!", cx, 152, F18, 1.0f, C_YELLOW);
      } else {
        snprintf(buf, sizeof buf, "BEST %u", (unsigned)s_best);
        text(band, bandY, buf, cx, 152, F12, 1.0f, C_GREY);
      }
      snprintf(buf, sizeof buf, "Stage %u   %u coins   %u m", (unsigned)s_stage,
               (unsigned)s_coins, (unsigned)(s_bankedM + (uint32_t)s_finalRun));
      text(band, bandY, buf, cx, 186, F12, 1.0f, C_WHITE);
      if (s_stateT > RUN_END_LOCKOUT_S) text(band, bandY, "TAP TO RUN AGAIN", cx, 226, F12, 1.0f, C_WHITE);
      break;

    case State::Run:
      break;
  }
}

State    state()     { return s_state; }
float    distanceM() { return runM(); }
float    speedMS()   { return s_speed; }
uint32_t score()     { return currentScore(); }
uint8_t  stage()     { return s_stage; }

} // namespace encounter
