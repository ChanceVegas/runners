// encounter.cpp — lane-chase gameplay. Obstacles and coins live at fixed WORLD
// positions (wz, metres along the road); their depth on screen is z = wz - travel,
// so the only thing that moves each tick is travel. That keeps interpolation trivial.
//
// Flow: Ready (idle attract road; game_state's menu draws on top) -> startArcade() or
// startBattle() -> Countdown -> Run -> hit -> Crash -> GameOver, or goal -> StageClear.
// ARCADE: StageClear -> tap -> next stage (faster, more walls); GameOver -> tap -> new
// game, or MENU button -> finished(). Score = metres across stages + coins x
// COIN_POINTS; arcade best saved to flash (NVS) at game over.
// BATTLE: one run vs an overworld enemy; rules live in battle.* (Pursuit -> Overtake
// -> Hunt). Hits stumble instead of crash; shields (Run energy) absorb hits; the
// battle outcome ends the run; either end screen -> tap -> finished().
//
// Art is drawn with fillRect/fillEllipse/fillTriangle (no bitmaps), so the open
// sprite colour-key issue (CARRY-1) can't affect it. Text uses LovyanGFX's smooth
// FreeSansBold fonts.
#include "encounter.h"
#include "lanes.h"
#include "scenery.h"
#include "hud.h"
#include "battle.h"
#include "audio.h"
#include "settings.h"
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
using encounter::Mode;

// ---- World objects ------------------------------------------------------------
using Kind = encounter::Block;    // Barrier = jump, Wall = dodge, Overhead = duck

struct Obstacle { bool active; Kind kind; int8_t lane; float wz; };
struct Coin     { bool active; bool high; int8_t lane; float wz; };
struct Orb      { bool active; int8_t lane; float wz; };     // battle: strikes the enemy

Obstacle s_obst[OBST_POOL];
Coin     s_coin[COIN_POOL];
constexpr int ORB_POOL = 8;
Orb      s_orb[ORB_POOL];

// ---- Game state ---------------------------------------------------------------
State s_state = State::Ready;
float s_stateT = 0;                        // s in the current state

float s_travel = 0, s_prevTravel = 0;      // m
float s_laneX = 0, s_prevLaneX = 0;        // lanes, -1..1 (animated)
int8_t s_laneTarget = 0;
float s_jumpY = 0, s_prevJumpY = 0;        // px above the ground
float s_jumpV = 0;                         // px/s, + = up
float s_duckT = 0;                         // s of duck left (0 = standing)
float s_jumpMul = 1.0f;                    // battle: SPRING sneakers (arcade: 1)
const char* s_dropName = nullptr;          // battle: drink dropped on DEFEATED (display)
uint32_t s_arcadeBank = 0;                 // arcade: coins to bank to the wallet (taken by game_state)
uint32_t s_arcadeBanked = 0;               // arcade: shown on the game-over screen
bool  s_duckQueued = false;                // DUCK tapped mid-air: duck on landing
float s_speed = 0;                         // m/s
float s_speedMax = RUN_SPEED_MAX;          // this stage's cap
int   s_wallChance = OBST_WALL_CHANCE;     // this stage's wall %
int   s_duckChance = OBST_DUCK_CHANCE;     // this stage's duck-bar %
float s_runStart = 0;                      // travel when this stage started
float s_nextRowWz = 0;
float s_prevRowWz = -1e9f;                 // last row spawned; trails must not reach back into it
float s_finalRun = 0;                      // m run when the stage ended

uint8_t  s_stage = 1;
uint32_t s_coins = 0;                      // coins this session
uint32_t s_bankedM = 0;                    // metres from cleared stages
uint32_t s_best = 0;
bool     s_newBest = false;
Preferences s_prefs;

// Mode: Arcade (menu -> endless stages, best score) or Battle (from the overworld:
// one stage vs an enemy, shields from Run energy, result handed back to game_state).
encounter::Mode s_mode = encounter::Mode::Arcade;
float s_goalM = RUN_GOAL_M;                // stage length this run
float s_goalFromM = 0.0f;                  // goal bar starts here (battle: moves after a pass)
uint8_t s_shields = 0, s_shieldsUsed = 0;  // chase: hits absorbed
float s_invuln = 0;                        // s of post-shield invulnerability left
float s_stumbleT = 0;                      // battle: s of stumble left (slowed, no hits)
float s_shakeT = 0, s_hitFlashT = 0;       // battle: hit impact (camera shake / red border)
battle::Outcome s_outcome = battle::Outcome::None;   // how the battle ended
bool s_done = false;                       // chase finished / arcade exit requested
encounter::Result s_result = {};

// ---- Render-side (latched in beginRender) ---------------------------------------
float d_travel = 0, d_laneX = 0, d_jumpY = 0, d_time = 0;
bool  d_blink = false;                     // latched per frame: all bands agree

// One depth-sorted list of everything on the road, built once per frame.
enum class DrawKind : uint8_t { Obstacle, Coin, Orb, Enemy, Runner };
struct DrawItem { DrawKind kind; uint8_t idx; int16_t x, y; float s; float z; };
DrawItem s_draw[OBST_POOL + COIN_POOL + 1];
int s_nDraw = 0;
int16_t d_runnerX = LCD_WIDTH / 2;         // runner screen x this frame (pursuer overlay)

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
  for (auto& o : s_orb)  o.active = false;
}

const char* kindName(Kind k) {
  return k == Kind::Wall ? "WALL" : (k == Kind::Overhead ? "DUCK BAR" : "BARRIER");
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
    const int roll = (int)(rnd() % 100);
    const Kind k = roll < s_wallChance ? Kind::Wall
                 : roll < s_wallChance + s_duckChance ? Kind::Overhead : Kind::Barrier;
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
    // Trail leading into the gap. Never reach back past the previous row + margin:
    // at the late-game 18 m gap a full 15 m trail would start 3 m past that row and
    // steer the player into its blocked lane. Drop the far coins instead.
    const float minWz = s_prevRowWz + COIN_TRAIL_MARGIN_M;
    for (int i = 0; i < COIN_TRAIL_N; ++i) {
      const float cz = wz - (COIN_TRAIL_N - i) * COIN_TRAIL_STEP_M;
      if (cz >= minWz) spawnCoin(false, (int8_t)open, cz);
    }
  }
  s_prevRowWz = wz;
}

void setState(State st) { s_state = st; s_stateT = 0; }

void finish(bool won, bool exitToMenu) {
  const bool beaten = s_outcome == battle::Outcome::Defeated ||
                      (s_mode == Mode::Battle && battle::passedIt());   // passed it = beaten
  s_result = { won, beaten, s_outcome == battle::Outcome::GotAway,
               s_coins, s_shieldsUsed, exitToMenu };
  s_done = true;
}

void beginStage() {
  clearWorld();
  const float step = (s_stage - 1) * STAGE_SPEED_STEP;
  s_speed = fminf(RUN_SPEED_START + step, STAGE_SPEED_CAP);
  s_speedMax = fminf(RUN_SPEED_MAX + step, STAGE_SPEED_CAP);
  s_wallChance = OBST_WALL_CHANCE;
  s_duckChance = OBST_DUCK_CHANCE + (s_stage - 1) * STAGE_DUCK_STEP;
  if (s_duckChance > STAGE_DUCK_CAP) s_duckChance = STAGE_DUCK_CAP;
  if (s_mode == encounter::Mode::Battle) {                   // walls rare in a Pursuit
    s_speed += battle::speedBonus();                         // rank: faster road
    s_speedMax = fminf(s_speedMax + battle::speedBonus(), STAGE_SPEED_CAP);
    s_wallChance = BATTLE_WALL_CHANCE;
    s_duckChance = BATTLE_DUCK_CHANCE;
  }
  s_shakeT = s_hitFlashT = 0;
  s_laneTarget = 0;
  s_laneX = s_prevLaneX = 0;
  s_jumpY = s_prevJumpY = s_jumpV = 0;
  s_duckT = 0;
  s_duckQueued = false;
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
  s_prevRowWz = -1e9f;
  setState(State::Run);
}

void crash() {
  s_finalRun = s_travel - s_runStart;
  audio::play(audio::Sfx::Hit);
  setState(State::Crash);
}

void gameOver() {
  const uint32_t sc = currentScore();
  if (s_mode == encounter::Mode::Arcade) {          // bank a share of the run's coins
    s_arcadeBanked = s_coins * ARCADE_WALLET_PCT / 100;
    s_arcadeBank += s_arcadeBanked;
  }
  if (s_mode == encounter::Mode::Arcade && sc > s_best) {
    s_best = sc;
    s_newBest = true;
    s_prefs.putUInt("best", s_best);      // rare flash write: once per game over
  }
  setState(State::GameOver);
  if (s_mode == encounter::Mode::Battle) audio::play(audio::Sfx::Lose);
}

// Battle: the Hunt timer ran out. No crash animation — the enemy just pulls away.
void gotAway() {
  s_finalRun = s_travel - s_runStart;
  audio::play(audio::Sfx::Lose);
  clearWorld();
  setState(State::GameOver);
}

void stageClear() {
  s_finalRun = s_travel - s_runStart;
  audio::play(audio::Sfx::Win);
  s_bankedM += (uint32_t)s_finalRun;
  clearWorld();
  setState(State::StageClear);
}

// ---- Drawing helpers (shared, see hud.h) ------------------------------------------
using hud::rowsHit;
using hud::rect;
using hud::text;

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
  } else if (o.kind == Kind::Overhead) {                    // duck bar: beam on two posts
    const int32_t lo = (int32_t)(OBST_DUCK_LOW_PX * s), top = (int32_t)(OBST_DUCK_TOP_PX * s) + 1;
    const int32_t post = w / 12 + 1;
    rect(b, bandY, x, yb - top, post, top, rgb565(80, 80, 88));                  // posts
    rect(b, bandY, x + w - post, yb - top, post, top, rgb565(80, 80, 88));
    const int32_t bh = top - lo;                                                 // beam
    // Yellow/black overhead-clearance beam (barriers are orange/white and low), with
    // a white "duck" arrow pointing down, so the two read apart at a glance.
    rect(b, bandY, x, yb - top, w, bh, rgb565(255, 210, 0));
    for (int i = 0; i < 4; ++i)                                                  // black blocks
      rect(b, bandY, x + i * w / 4, yb - top, w / 8 + 1, bh, rgb565(20, 20, 20));
    if (rowsHit(yb - lo - bh / 2, yb - lo + 1, bandY, b.height()) && bh > 6) {
      const int32_t aw = w / 6 + 2, ay = yb - lo - bandY;                        // arrow under the beam
      b.fillTriangle(d.x - aw, ay - bh / 2, d.x + aw, ay - bh / 2, d.x, ay, C_WHITE);
    }
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

void drawOrb(lgfx::LGFX_Sprite& b, int32_t bandY, const DrawItem& d) {
  const float s = d.s;
  const int32_t r = (int32_t)(26.0f * s) + 2;
  const int32_t pulse = (int32_t)(sinf(d_time * 10.0f + d.z) * 2.0f * s);
  const int32_t yc = d.y - (int32_t)(55.0f * s);
  if (!rowsHit(yc - r - 4, yc + r + 4, bandY, b.height())) return;
  b.fillCircle(d.x, yc - bandY, r + pulse, rgb565(40, 140, 220));       // glow
  b.fillCircle(d.x, yc - bandY, r * 2 / 3, rgb565(150, 230, 255));      // core
  b.fillCircle(d.x - r / 4, yc - r / 4 - bandY, r / 4 + 1, rgb565(255, 255, 255));
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

  const bool stumbling = s_stumbleT > 0.0f;
  if (!stumbling && s_invuln > 0.0f && ((int32_t)(d_time * 12.0f) & 1)) return;   // shield blink

  if (stumbling && !down) {                             // tripping: pitched forward, arms out
    rect(b, bandY, cx - 10, feet - 18, 8, 18, SKIN);
    rect(b, bandY, cx + 4,  feet - 12, 14, 7, SKIN);    // trailing leg kicked back
    rect(b, bandY, cx - 14, feet - 40, 28, 22, JERSEY);
    rect(b, bandY, cx - 30, feet - 44, 16, 6, SKIN);    // arms flung out
    rect(b, bandY, cx + 14, feet - 44, 16, 6, SKIN);
    rect(b, bandY, cx - 9, feet - 56, 18, 16, SKIN);
    rect(b, bandY, cx - 10, feet - 58, 20, 7, HAIR);
    return;
  }

  if (s_duckT > 0.0f && !down) {                        // ducking: low crouch, 40 px tall
    rect(b, bandY, cx - 16, feet - 10, 12, 10, SHOE);
    rect(b, bandY, cx + 4,  feet - 10, 12, 10, SHOE);
    rect(b, bandY, cx - 16, feet - 20, 32, 10, SHORTS);
    rect(b, bandY, cx - 18, feet - 34, 36, 16, JERSEY);
    rect(b, bandY, cx - 18, feet - 34, 36, 4, JERSEY_HI);
    rect(b, bandY, cx - 22, feet - 26, 6, 12, SKIN);    // arms tucked
    rect(b, bandY, cx + 16, feet - 26, 6, 12, SKIN);
    rect(b, bandY, cx - 8,  feet - 40, 16, 10, HAIR);   // head down, back of the head
    return;
  }

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

// Battle end screens: rank change from this outcome (same rule game_state saves).
void rankNote(lgfx::LGFX_Sprite& b, int32_t bandY, int32_t y) {
  char buf[40];
  // Passing it locked in DEFEATED, even if it caught you again afterwards.
  const battle::Outcome o = battle::passedIt() ? battle::Outcome::Defeated : s_outcome;
  const uint16_t before = battle::rankPts(), after = battle::ptsAfter(before, o);
  const uint8_t r0 = battle::rankOf(before), r1 = battle::rankOf(after);
  if (r1 > r0) { snprintf(buf, sizeof buf, "RANK UP!  %u -> %u", r0, r1); text(b, bandY, buf, LCD_WIDTH / 2, y, hud::F12, 1.0f, C_GREEN); }
  else if (r1 < r0) { snprintf(buf, sizeof buf, "RANK DOWN  %u -> %u", r0, r1); text(b, bandY, buf, LCD_WIDTH / 2, y, hud::F12, 1.0f, C_RED); }
  else {
    snprintf(buf, sizeof buf, "RANK %u  (%u / %u)", r1, (unsigned)(after % RANK_PTS_PER), (unsigned)RANK_PTS_PER);
    text(b, bandY, buf, LCD_WIDTH / 2, y, hud::F9, 1.0f, C_GREY);
  }
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
      // Keep float precision over long idles: wrap by a whole number of stripe (6 m,
      // period 12) and curve (220 m) cycles so the road doesn't visibly jump. Only
      // the hashed scenery re-rolls, once every few hours of attract mode.
      if (s_travel > 100000.0f) { s_travel -= 660.0f; s_prevTravel -= 660.0f; }
      return;                              // game_state's menu starts a mode

    case State::Countdown:
      if (settings::muteCornerTapped(in)) audio::setMuted(!audio::muted());
      s_travel += RUN_ATTRACT_SPEED * dt;
      if (s_stateT >= RUN_COUNTDOWN_S) startRunning();
      return;

    case State::Crash:
      if (s_stateT >= RUN_CRASH_S) gameOver();
      return;

    case State::StageClear:
      s_travel += RUN_ATTRACT_SPEED * dt;
      if (s_done || s_stateT <= RUN_END_LOCKOUT_S || !in.pressed) return;
      if (s_mode == Mode::Battle) finish(true, false);
      else { ++s_stage; beginStage(); }
      return;

    case State::GameOver:
      if (s_done || s_stateT <= RUN_END_LOCKOUT_S || !in.pressed) return;
      if (s_mode == Mode::Battle) finish(false, false);
      else if (in.pointX >= 0 && in.pointX < 120 && in.pointY < 56) finish(false, true);  // MENU
      else newGame();
      return;

    case State::Run:
      break;
  }

  // Mute corner (top-right): input never turns these taps into lane steps.
  if (settings::muteCornerTapped(in)) audio::setMuted(!audio::muted());

  // Controls.
  if (in.laneStep < 0 && s_laneTarget > -1) --s_laneTarget;
  if (in.laneStep > 0 && s_laneTarget <  1) ++s_laneTarget;
  const bool airborne = s_jumpY > 0.0f || s_jumpV > 0.0f;
  const bool jumpOk = in.jumpPressed && !airborne;
  if (jumpOk) {                                                                   // jump cancels a duck
    s_jumpV = RUN_JUMP_VEL_PX_S * s_jumpMul; s_duckT = 0; s_duckQueued = false;
    audio::play(audio::Sfx::Jump);
  }
  if (in.duckPressed && !jumpOk) {
    if (airborne) { s_jumpV = -RUN_DUCK_DROP_PX_S; s_duckQueued = true; }          // fast fall
    else s_duckT = RUN_DUCK_S;
    audio::play(audio::Sfx::Duck);
  }
#if DEBUG_INPUT_LOG
  if (in.laneStep) Serial.printf("[in] tap %s -> lane %d at %d m (x%d y%d)\n",
                                 in.laneStep < 0 ? "LEFT" : "RIGHT", s_laneTarget,
                                 (int)(s_travel - s_runStart), in.tapX, in.tapY);
  if (in.jumpPressed) Serial.printf("[in] tap MIDDLE: jump%s at %d m (x%d y%d)\n",
                                    jumpOk ? "" : " (ignored: airborne)",
                                    (int)(s_travel - s_runStart), in.tapX, in.tapY);
  if (in.duckPressed) Serial.printf("[in] tap BOTTOM: duck%s at %d m (x%d y%d)\n",
                                    airborne ? " (fast fall)" : "", (int)(s_travel - s_runStart),
                                    in.tapX, in.tapY);
#endif

  const float step = RUN_LANE_SPEED * dt;
  if (s_laneX < s_laneTarget) s_laneX = fminf(s_laneX + step, (float)s_laneTarget);
  else if (s_laneX > s_laneTarget) s_laneX = fmaxf(s_laneX - step, (float)s_laneTarget);

  if (s_jumpY > 0.0f || s_jumpV > 0.0f) {
    s_jumpV -= RUN_GRAVITY_PX_S2 * dt;
    s_jumpY += s_jumpV * dt;
    if (s_jumpY <= 0.0f) {
      s_jumpY = 0.0f; s_jumpV = 0.0f;
      if (s_duckQueued) { s_duckQueued = false; s_duckT = RUN_DUCK_S; }
    }
  }
  if (s_duckT > 0.0f) s_duckT -= dt;

  s_speed = fminf(s_speed + RUN_SPEED_RAMP * dt, s_speedMax);
  const bool battleMode = (s_mode == Mode::Battle);
  if (s_stumbleT > 0.0f) s_stumbleT -= dt;
  if (s_shakeT > 0.0f) s_shakeT -= dt;
  if (s_hitFlashT > 0.0f) s_hitFlashT -= dt;
  s_travel += s_speed * (s_stumbleT > 0.0f ? BATTLE_STUMBLE_SPEED : 1.0f) * dt;
  const float run = s_travel - s_runStart;
  const float prog = run / s_goalM;

  // Regular obstacle rows: arcade always, battles only during the Pursuit (in the
  // Hunt the enemy's attacks are the obstacles).
  const bool rows = !battleMode || battle::phase() == battle::Phase::Pursuit;
  while (rows && s_nextRowWz < s_travel + OBST_SPAWN_Z && s_nextRowWz < s_runStart + s_goalM - 20.0f) {
    spawnRow(s_nextRowWz);
    s_nextRowWz += (OBST_GAP_START_M + (OBST_GAP_MIN_M - OBST_GAP_START_M) * fminf(prog, 1.0f)) *
                   (battleMode ? BATTLE_ROW_GAP_MUL * battle::rowGapMul() : 1.0f);
  }

  const float pz = playerZ();
  if (s_invuln > 0.0f) s_invuln -= dt;
  if (battleMode) battle::update(dt, s_travel, run, pz, s_stumbleT > 0.0f);

  // Orbs (battle): collect or recycle. Any height counts — they float at waist level.
  for (auto& o : s_orb) {
    if (!o.active) continue;
    const float z = o.wz - s_travel;
    if (z < 0.5f) { o.active = false; continue; }
    if (fabsf(z - pz) < BATTLE_ORB_HIT_DEPTH_M && fabsf(s_laneX - o.lane) < 0.5f) {
      o.active = false;
      battle::onOrb();
      audio::play(audio::Sfx::Orb);
    }
  }

  // Coins: collect or recycle.
  for (auto& c : s_coin) {
    if (!c.active) continue;
    const float z = c.wz - s_travel;
    if (z < 0.5f) { c.active = false; continue; }
    if (fabsf(z - pz) < COIN_HIT_DEPTH_M && fabsf(s_laneX - c.lane) < 0.5f) {
      const bool reach = c.high ? (s_jumpY >= COIN_HIGH_MIN_JUMP) : (s_jumpY <= COIN_LOW_MAX_JUMP);
      if (reach) { c.active = false; ++s_coins; audio::play(audio::Sfx::Coin); }
    }
  }

  // Obstacles: collide or recycle.
  for (auto& o : s_obst) {
    if (!o.active) continue;
    const float z = o.wz - s_travel;
    if (z < 0.5f) { o.active = false; continue; }
    if (s_invuln <= 0.0f && !(battleMode && battle::passing()) && fabsf(z - pz) < OBST_HIT_DEPTH_M && fabsf(s_laneX - o.lane) < OBST_HIT_LANE) {
      const bool hit = o.kind == Kind::Wall ||
                       (o.kind == Kind::Barrier && s_jumpY < OBST_CLEAR_PX) ||
                       (o.kind == Kind::Overhead && s_duckT <= 0.0f);
      if (hit) {
        if (s_shields > 0) {                       // shield absorbs the hit
          --s_shields; ++s_shieldsUsed;
          o.active = false;
          s_invuln = SHIELD_INVULN_S;
          if (battleMode) battle::onShieldBlock();
#if DEBUG_INPUT_LOG
          Serial.printf("[hit] shield absorbed %s at %d m (%u left)\n",
                        kindName(o.kind), (int)run, (unsigned)s_shields);
#endif
          continue;
        }
#if DEBUG_INPUT_LOG
        Serial.printf("[hit] %s lane %d, runner lane %.2f jumpY %d at %d m\n",
                      kindName(o.kind), o.lane, s_laneX,
                      (int)s_jumpY, (int)run);
#endif
        if (battleMode) {                          // battles: stumble, never instant death
          o.active = false;
          battle::onObstacleHit(o.kind == Kind::Wall);
          s_stumbleT = battle::stumbleSeconds(o.kind == Kind::Wall);
          s_invuln = s_stumbleT;
          s_shakeT = BATTLE_HIT_SHAKE_S;           // impact: shake + red border + thud
          audio::play(audio::Sfx::Hit);
          s_hitFlashT = BATTLE_HIT_FLASH_S;
          continue;
        }
        crash();
        return;
      }
    }
  }

  if (battleMode) {
    s_outcome = battle::outcome();
    if (s_outcome == battle::Outcome::Escaped || s_outcome == battle::Outcome::Defeated) stageClear();
    else if (s_outcome == battle::Outcome::Caught) crash();
    else if (s_outcome == battle::Outcome::GotAway) gotAway();
  } else if (run >= s_goalM) {
    stageClear();
  }
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
  d_blink  = ((millis() / 450) & 1) == 0;
  float shake = 0.0f;
  if (s_shakeT > 0.0f && s_state == State::Run) {       // decaying side-to-side jolt
    const float k = s_shakeT / BATTLE_HIT_SHAKE_S;
    shake = BATTLE_HIT_SHAKE_PX * k * (((millis() / 35) & 1) ? 1.0f : -1.0f);
  }
  lanes::setShake(shake);
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
  for (int i = 0; i < ORB_POOL; ++i) {
    const Orb& o = s_orb[i];
    if (!o.active) continue;
    const float z = o.wz - d_travel;
    if (!lanes::project(z, (float)o.lane, sx, sy, s) || s < 0.03f) continue;
    s_draw[s_nDraw++] = { DrawKind::Orb, (uint8_t)i, (int16_t)sx, (int16_t)sy, s, z };
  }
  const float pz = playerZ();
  if (s_mode == Mode::Battle && battle::enemyOnRoad() && s_state != State::Ready) {
    const float z = pz + battle::enemyZAhead();
    if (lanes::project(z, battle::enemyLane(), sx, sy, s))
      s_draw[s_nDraw++] = { DrawKind::Enemy, 0, (int16_t)sx, (int16_t)sy, s, z };
  }
  if (s_state != State::Ready) {
    if (lanes::project(pz, d_laneX, sx, sy, s)) {
      s_draw[s_nDraw++] = { DrawKind::Runner, 0, (int16_t)sx, (int16_t)sy, s, pz };
      d_runnerX = (int16_t)sx;
    }
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
      case DrawKind::Orb:      drawOrb(band, bandY, d);      break;
      case DrawKind::Enemy:    battle::drawEnemyOnRoad(band, bandY, d.x, d.y, d.s, d_time); break;
      case DrawKind::Runner:   drawRunner(band, bandY, d);   break;
    }
  }
  // Pursuit: the enemy is behind the runner — drawn over the road, from the bottom edge.
  if (s_mode == Mode::Battle && (s_state == State::Run || s_state == State::Countdown))
    battle::drawPursuer(band, bandY, d_runnerX, d_time);
}

void composeHud(lgfx::LGFX_Sprite& band, int32_t bandY) {
  char buf[40];
  using hud::F9; using hud::F12; using hud::F18; using hud::F24;
  const bool chase = (s_mode == Mode::Battle);
  const int32_t cx = LCD_WIDTH / 2;
  const bool blink = d_blink;

  // In-game HUD: escape progress bar, score, stage, coins.
  if (s_state == State::Run || s_state == State::Countdown || s_state == State::Crash) {
    const bool showGoal = !chase || battle::phase() == battle::Phase::Pursuit;
    const float p = fminf(fmaxf(runM() - s_goalFromM, 0.0f) / (s_goalM - s_goalFromM), 1.0f);
    if (showGoal) {
      rect(band, bandY, 10, 6, LCD_WIDTH - 20, 10, C_SHADOW);
      rect(band, bandY, 12, 8, (int32_t)((LCD_WIDTH - 24) * (s_state == State::Countdown ? 0 : p)),
           6, C_GREEN);
    }
    snprintf(buf, sizeof buf, "%u", (unsigned)currentScore());
    text(band, bandY, buf, 70, 34, F12, 1.0f, C_WHITE, 130);
    if (chase) {
      battle::composeHud(band, bandY, d_time);
      for (uint8_t i = 0; i < s_shields; ++i)            // shield pips, top right
        hud::rect(band, bandY, LCD_WIDTH - 40 - i * 16, 56, 12, 8, rgb565(90, 200, 255));
    } else {
      snprintf(buf, sizeof buf, "STAGE %u", (unsigned)s_stage);
      text(band, bandY, buf, cx, 34, F9, 1.0f, C_GREY);
    }
    drawCoinIcon(band, bandY, LCD_WIDTH - 130, 34);   // left of the mute corner
    snprintf(buf, sizeof buf, "%u", (unsigned)s_coins);
    text(band, bandY, buf, LCD_WIDTH - 90, 34, F12, 1.0f, C_YELLOW, 70);
    settings::drawMuteIcon(band, bandY);
  }

  // Battle hit: red impact border (thicker than the crash one, fades by thinning).
  if (chase && s_state == State::Run && s_hitFlashT > 0.0f) {
    const int32_t t = 6 + (int32_t)(12.0f * s_hitFlashT / BATTLE_HIT_FLASH_S);
    rect(band, bandY, 0, 0, LCD_WIDTH, t, C_RED);
    rect(band, bandY, 0, LCD_HEIGHT - t, LCD_WIDTH, t, C_RED);
    rect(band, bandY, 0, 0, t, LCD_HEIGHT, C_RED);
    rect(band, bandY, LCD_WIDTH - t, 0, t, LCD_HEIGHT, C_RED);
  }

  // Always-on (during a run) marks where the jump/duck split is: short ticks on the
  // middle zone's edges at INPUT_ZONE_DUCK_Y. Four rects, no text.
  if (s_state == State::Run) {
    rect(band, bandY, INPUT_ZONE_LEFT_X, INPUT_ZONE_DUCK_Y - 1, 14, 3, C_GREY);
    rect(band, bandY, INPUT_ZONE_RIGHT_X - 14, INPUT_ZONE_DUCK_Y - 1, 14, 3, C_GREY);
    rect(band, bandY, INPUT_ZONE_LEFT_X, INPUT_ZONE_DUCK_Y - 1, 2, LCD_HEIGHT - INPUT_ZONE_DUCK_Y + 1, C_GREY);
    rect(band, bandY, INPUT_ZONE_RIGHT_X, INPUT_ZONE_DUCK_Y - 1, 2, LCD_HEIGHT - INPUT_ZONE_DUCK_Y + 1, C_GREY);
  }

  // Tap-zone hints: on the title, through the countdown and the first seconds of a run.
  const bool hints = s_state == State::Countdown ||
                     (s_state == State::Run && s_stateT < RUN_HINT_S);
  if (hints) {
    rect(band, bandY, INPUT_ZONE_LEFT_X, INPUT_ZONE_DUCK_Y - 40, 2, LCD_HEIGHT - INPUT_ZONE_DUCK_Y + 40, C_GREY);
    rect(band, bandY, INPUT_ZONE_RIGHT_X, INPUT_ZONE_DUCK_Y - 40, 2, LCD_HEIGHT - INPUT_ZONE_DUCK_Y + 40, C_GREY);
    text(band, bandY, "< LANE", INPUT_ZONE_LEFT_X / 2, LCD_HEIGHT - 15, F12, 1.0f, C_WHITE, 150);
    rect(band, bandY, INPUT_ZONE_LEFT_X, INPUT_ZONE_DUCK_Y, INPUT_ZONE_RIGHT_X - INPUT_ZONE_LEFT_X, 2, C_GREY);
    text(band, bandY, "JUMP", cx, INPUT_ZONE_DUCK_Y - 16, F12, 1.0f, C_WHITE, 150);
    text(band, bandY, "DUCK", cx, LCD_HEIGHT - 15, F12, 1.0f, C_WHITE, 150);
    text(band, bandY, "LANE >", (INPUT_ZONE_RIGHT_X + LCD_WIDTH) / 2, LCD_HEIGHT - 15, F12, 1.0f,
         C_WHITE, 150);
  }

  switch (s_state) {
    case State::Ready:                     // menu (game_state) draws over this
      break;

    case State::Countdown: {
      const int n = 3 - (int)(s_stateT / (RUN_COUNTDOWN_S / 4.0f));
      if (n >= 1) { snprintf(buf, sizeof buf, "%d", n); text(band, bandY, buf, cx, 120, F24, 2.0f, C_YELLOW); }
      else text(band, bandY, "GO!", cx, 120, F24, 1.8f, C_GREEN);
      if (chase) {
        snprintf(buf, sizeof buf, "%s IS CHASING YOU", battle::enemyName());
        text(band, bandY, buf, cx, 176, F18, 1.0f, C_RED);
        snprintf(buf, sizeof buf, "RANK %u", (unsigned)battle::rank());
        text(band, bandY, buf, cx, 204, F12, 1.0f, C_WHITE);
      } else if (s_stage > 1) {
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
      if (chase && s_outcome == battle::Outcome::Defeated) {
        text(band, bandY, "DEFEATED!", cx, 70, F24, 1.3f, C_YELLOW);
        snprintf(buf, sizeof buf, "You beat the %s", battle::enemyName());
        text(band, bandY, buf, cx, 122, F18, 1.0f, C_WHITE);
        snprintf(buf, sizeof buf, "%u coins collected", (unsigned)s_coins);
        text(band, bandY, buf, cx, 160, F12, 1.0f, C_YELLOW);
        if (s_dropName) {
          snprintf(buf, sizeof buf, "It dropped a %s drink!", s_dropName);
          text(band, bandY, buf, cx, 184, F12, 1.0f, rgb565(120, 230, 255));
        }
        rankNote(band, bandY, 206);
        if (s_stateT > RUN_END_LOCKOUT_S && blink)
          text(band, bandY, "TAP TO RETURN TO MAP", cx, 236, F12, 1.0f, C_WHITE);
        break;
      }
      text(band, bandY, "ESCAPED!", cx, 70, F24, 1.3f, C_GREEN);
      if (chase) {
        snprintf(buf, sizeof buf, "You lost the %s", battle::enemyName());
        text(band, bandY, buf, cx, 122, F18, 1.0f, C_WHITE);
        snprintf(buf, sizeof buf, "%u coins collected", (unsigned)s_coins);
        text(band, bandY, buf, cx, 160, F12, 1.0f, C_YELLOW);
        rankNote(band, bandY, 186);
        if (s_stateT > RUN_END_LOCKOUT_S && blink)
          text(band, bandY, "TAP TO RETURN TO MAP", cx, 210, F12, 1.0f, C_WHITE);
        break;
      }
      snprintf(buf, sizeof buf, "STAGE %u CLEAR", (unsigned)s_stage);
      text(band, bandY, buf, cx, 122, F18, 1.0f, C_WHITE);
      snprintf(buf, sizeof buf, "SCORE %u", (unsigned)currentScore());
      text(band, bandY, buf, cx, 160, F12, 1.0f, C_YELLOW);
      if (s_stateT > RUN_END_LOCKOUT_S && blink)
        text(band, bandY, "TAP FOR NEXT STAGE", cx, 210, F12, 1.0f, C_WHITE);
      break;

    case State::GameOver:
      if (chase && s_outcome == battle::Outcome::GotAway) {
        text(band, bandY, "GOT AWAY!", cx, 62, F24, 1.3f, rgb565(255, 140, 60));
        snprintf(buf, sizeof buf, "The %s outran you", battle::enemyName());
        text(band, bandY, buf, cx, 114, F18, 1.0f, C_WHITE);
        snprintf(buf, sizeof buf, "%u coins lost", (unsigned)s_coins);
        text(band, bandY, buf, cx, 152, F12, 1.0f, C_RED);
        rankNote(band, bandY, 180);
        if (battle::passedIt())
          text(band, bandY, "You passed it earlier: defeat bonus kept", cx, 202, F9, 1.0f,
               rgb565(120, 230, 255));
        if (s_stateT > RUN_END_LOCKOUT_S)
          text(band, bandY, "TAP TO RETURN TO MAP", cx, 228, F12, 1.0f, C_WHITE);
        break;
      }
      text(band, bandY, "CAUGHT!", cx, 62, F24, 1.3f, C_RED);
      if (chase) {
        snprintf(buf, sizeof buf, "The %s got you", battle::enemyName());
        text(band, bandY, buf, cx, 114, F18, 1.0f, C_WHITE);
        snprintf(buf, sizeof buf, "%u coins lost", (unsigned)s_coins);
        text(band, bandY, buf, cx, 152, F12, 1.0f, C_RED);
        rankNote(band, bandY, 180);
        if (battle::passedIt())
          text(band, bandY, "You passed it earlier: defeat bonus kept", cx, 202, F9, 1.0f,
               rgb565(120, 230, 255));
        if (s_stateT > RUN_END_LOCKOUT_S)
          text(band, bandY, "TAP TO RETURN TO MAP", cx, 228, F12, 1.0f, C_WHITE);
        break;
      }
      hud::rect(band, bandY, 8, 8, 104, 40, rgb565(40, 40, 48));     // MENU button
      hud::frame(band, bandY, 8, 8, 104, 40, C_GREY);
      text(band, bandY, "MENU", 60, 28, F12, 1.0f, C_WHITE);
      snprintf(buf, sizeof buf, "SCORE %u", (unsigned)currentScore());
      text(band, bandY, buf, cx, 114, F18, 1.0f, C_WHITE);
      if (s_newBest) {
        if (blink) text(band, bandY, "NEW BEST!", cx, 152, F18, 1.0f, C_YELLOW);
      } else {
        snprintf(buf, sizeof buf, "BEST %u", (unsigned)s_best);
        text(band, bandY, buf, cx, 152, F12, 1.0f, C_GREY);
      }
      snprintf(buf, sizeof buf, "Stage %u   %u m   +%u coins to wallet", (unsigned)s_stage,
               (unsigned)(s_bankedM + (uint32_t)s_finalRun), (unsigned)s_arcadeBanked);
      text(band, bandY, buf, cx, 186, F12, 1.0f, C_WHITE);
      if (s_stateT > RUN_END_LOCKOUT_S) text(band, bandY, "TAP TO RUN AGAIN", cx, 226, F12, 1.0f, C_WHITE);
      break;

    case State::Run:
      break;
  }
}

void setDefeatDrop(const char* drinkName) { s_dropName = drinkName; }

uint32_t takeArcadeCoins() { const uint32_t c = s_arcadeBank; s_arcadeBank = 0; return c; }

void startArcade() {
  s_mode = Mode::Arcade;
  s_dropName = nullptr;
  s_jumpMul = 1.0f;                        // sneakers are battle stats; arcade stays fair
  s_goalM = RUN_GOAL_M;
  s_goalFromM = 0.0f;
  s_shields = s_shieldsUsed = 0;
  s_invuln = 0;
  s_stumbleT = 0;
  s_done = false;
  newGame();
}

void startBattle(uint8_t kind, uint8_t level, float goalM, uint8_t shields,
                 const battle::Stats& stats, uint16_t rankPts) {
  s_mode = Mode::Battle;
  s_jumpMul = stats.jumpMul;
  // s_dropName is set by setDefeatDrop() just before this call; keep it.
  s_goalM = goalM;
  s_goalFromM = 0.0f;
  s_shields = shields;
  s_shieldsUsed = 0;
  s_invuln = 0;
  s_stumbleT = 0;
  s_shakeT = s_hitFlashT = 0;
  s_outcome = battle::Outcome::None;
  s_done = false;
  s_stage = level;
  s_coins = 0;
  s_bankedM = 0;
  s_newBest = false;
  battle::start(kind, level, goalM, stats, rankPts);
  beginStage();
}

namespace engine {
void spawnObstacle(Block kind, int8_t lane, float wz) {
  ::spawnObstacle(kind, lane, wz);
}
void spawnOrb(int8_t lane, float wz) {
  for (auto& o : s_orb) if (!o.active) { o = { true, lane, wz }; return; }
}
void restartRace(float goalM) {
  s_goalFromM = s_travel - s_runStart;
  s_goalM = goalM;
  s_nextRowWz = s_travel + RUN_FIRST_ROW_M * 0.6f;   // rows resume a little way ahead
  s_prevRowWz = -1e9f;
}

void clearAhead() {
  const float from = s_travel + playerZ() + 2.0f;     // keep whatever is already at the runner
  for (auto& o : s_obst) if (o.active && o.wz > from) o.active = false;
  for (auto& c : s_coin) if (c.active && c.wz > from) c.active = false;
  for (auto& o : s_orb)  if (o.active && o.wz > from) o.active = false;
}
} // namespace engine

bool finished() { return s_done; }
Result result() { return s_result; }

void idle() {
  clearWorld();
  s_done = false;
  setState(State::Ready);
}

uint32_t bestScore() { return s_best; }

State    state()     { return s_state; }
float    distanceM() { return runM(); }
float    speedMS()   { return s_speed; }
uint32_t score()     { return currentScore(); }
uint8_t  stage()     { return s_stage; }

} // namespace encounter
