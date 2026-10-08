// battle.cpp — see battle.h. All per-kind behaviour (HP, attack rhythm, attack shape,
// orb drop rate, gap gain) lives in the KINDS table so enemies differ by data.
#include "battle.h"
#include "encounter.h"
#include "hud.h"
#include "color.h"
#include "config.h"
#include "board_config.h"
#include <Arduino.h>
#include <math.h>
#include <stdio.h>

namespace {

using battle::Phase;
using battle::Outcome;

enum class Attack : uint8_t { Barrier, TwoWalls, PhaseWall };

struct KindDef {
  const char* name;
  uint8_t hp;
  float attackS;
  int orbPct;
  Attack attack;
  uint16_t body, accent;
};

const KindDef KINDS[3] = {
  { "SHADE",   BATTLE_SHADE_HP,   BATTLE_SHADE_ATTACK_S,   BATTLE_SHADE_ORB_PCT,   Attack::Barrier,
    rgb565(120, 60, 170), rgb565(255, 255, 255) },
  { "BRUTE",   BATTLE_BRUTE_HP,   BATTLE_BRUTE_ATTACK_S,   BATTLE_BRUTE_ORB_PCT,   Attack::TwoWalls,
    rgb565(180, 40, 40),  rgb565(255, 220, 0) },
  { "PHANTOM", BATTLE_PHANTOM_HP, BATTLE_PHANTOM_ATTACK_S, BATTLE_PHANTOM_ORB_PCT, Attack::PhaseWall,
    rgb565(60, 190, 220), rgb565(220, 250, 255) },
};

const KindDef* s_k = &KINDS[0];
uint8_t s_level = 1;
float   s_goalM = 600.0f;
battle::Stats s_stats;

Phase   s_phase = Phase::Pursuit;
Outcome s_outcome = Outcome::None;
float s_gap = BATTLE_GAP_START_M, s_gapShown = BATTLE_GAP_START_M;
int   s_hearts = BATTLE_HEARTS;
int   s_hp = 5;
float s_travel = 0.0f, s_playerZ = 6.0f;

float s_overT = 0.0f;
float s_zAhead = 0.0f;
float s_lane = 0.0f, s_laneTarget = 0.0f, s_laneT = 0.0f;
float s_attackT = 0.0f;
float s_huntT = 0.0f;                // s left on the Hunt timer
float s_flashT = 0.0f;               // enemy hit flash
float s_blinkT = 0.0f;               // phantom teleport flicker

char  s_banner[32] = "";
float s_bannerT = 0.0f;
uint16_t s_bannerColor = 0xFFFF;

uint32_t s_rng = 7;
uint32_t rnd() { s_rng = s_rng * 1664525u + 1013904223u; return s_rng >> 8; }
float rndf(float a, float b) { return a + (b - a) * ((rnd() & 0xFFFF) / 65535.0f); }

void banner(const char* t, uint16_t c) {
  strncpy(s_banner, t, sizeof s_banner - 1);
  s_banner[sizeof s_banner - 1] = 0;
  s_bannerT = BATTLE_BANNER_S;
  s_bannerColor = c;
}

float gapGain() {
  return BATTLE_GAP_GAIN_L1 - (s_level - 1) * BATTLE_GAP_GAIN_STEP + s_stats.gapGainBonus;
}

void beginOvertake() {
  s_phase = Phase::Overtake;
  s_overT = 0.0f;
  s_zAhead = 0.5f;
  s_lane = s_laneTarget = 0.0f;
  encounter::engine::clearAhead();
  banner("IT'S AHEAD - CATCH IT!", rgb565(255, 220, 40));
}

void attack() {
  const int8_t el = (int8_t)lroundf(s_lane);
  const float wz = s_travel + s_playerZ + s_zAhead - 1.5f;   // just behind the enemy
  int8_t blocked[3] = {0, 0, 0};
  switch (s_k->attack) {
    case Attack::Barrier:                                   // Shade: barrier in its lane
      encounter::engine::spawnObstacle(false, el, wz);
      blocked[el + 1] = 1;
      break;
    case Attack::TwoWalls: {                                // Brute: walls in 2 lanes
      int8_t other = (el == 0) ? (int8_t)((rnd() & 1) ? 1 : -1) : 0;
      encounter::engine::spawnObstacle(true, el, wz);
      encounter::engine::spawnObstacle(true, other, wz);
      blocked[el + 1] = blocked[other + 1] = 1;
      break;
    }
    case Attack::PhaseWall: {                               // Phantom: wall, then blink away
      encounter::engine::spawnObstacle(true, el, wz);
      blocked[el + 1] = 1;
      int8_t nl;
      do { nl = (int8_t)((int)(rnd() % 3) - 1); } while (nl == el);
      s_lane = s_laneTarget = nl;
      s_blinkT = 0.25f;
      break;
    }
  }
  // Orb in a free lane of the same row, so it never sits inside an obstacle.
  if ((int)(rnd() % 100) < s_k->orbPct) {
    int8_t free[3]; int nf = 0;
    for (int8_t l = -1; l <= 1; ++l) if (!blocked[l + 1]) free[nf++] = l;
    if (nf) encounter::engine::spawnOrb(free[rnd() % nf], wz);
  }
}

// Heart icon at (x, y) top-left, ~16x14.
void heart(lgfx::LGFX_Sprite& b, int32_t bandY, int32_t x, int32_t y, bool full) {
  if (!hud::rowsHit(y, y + 15, bandY, b.height())) return;
  const uint16_t c = full ? rgb565(235, 50, 60) : rgb565(70, 40, 44);
  b.fillCircle(x + 4, y + 4 - bandY, 4, c);
  b.fillCircle(x + 11, y + 4 - bandY, 4, c);
  b.fillTriangle(x, y + 6 - bandY, x + 15, y + 6 - bandY, x + 7, y + 14 - bandY, c);
}

} // namespace

namespace battle {

void start(uint8_t kind, uint8_t level, float goalM, const Stats& stats) {
  s_k = &KINDS[kind < 3 ? kind : 0];
  s_level = level;
  s_goalM = goalM;
  s_stats = stats;
  s_rng = esp_random() | 1u;
  s_phase = Phase::Pursuit;
  s_outcome = Outcome::None;
  s_gap = s_gapShown = BATTLE_GAP_START_M + stats.startGapBonus;
  s_hearts = BATTLE_HEARTS + stats.extraHearts;
  s_hp = s_k->hp;
  s_huntT = 0.0f;
  s_zAhead = 0.0f;
  s_flashT = s_blinkT = 0.0f;
  s_bannerT = 0.0f;
}

void update(float dt, float travel, float runM, float playerZ, bool stumbling) {
  s_travel = travel;
  s_playerZ = playerZ;
  if (s_bannerT > 0) s_bannerT -= dt;
  if (s_flashT > 0) s_flashT -= dt;
  if (s_blinkT > 0) s_blinkT -= dt;
  if (s_outcome != Outcome::None) return;

  switch (s_phase) {
    case Phase::Pursuit:
      if (!stumbling) s_gap += gapGain() * dt;
      s_gapShown += (s_gap - s_gapShown) * fminf(1.0f, dt * 5.0f);
      if (s_gap >= BATTLE_GAP_ESCAPE_M || runM >= s_goalM) { s_outcome = Outcome::Escaped; return; }
      if (s_gap <= 0.0f) beginOvertake();
      break;

    case Phase::Overtake: {
      s_overT += dt;
      const float t = fminf(s_overT / BATTLE_OVERTAKE_S, 1.0f);
      const float e = t * t * (3.0f - 2.0f * t);
      s_zAhead = 0.5f + (BATTLE_HUNT_Z_M - 0.5f) * e;
      if (t >= 1.0f) {
        s_phase = Phase::Hunt;
        s_attackT = 0.8f;
        // Time limit from the fastest possible kill: every orb collected, every attack
        // dropping one at the kind's orb rate.
        const float orbsPerS = (s_k->orbPct / 100.0f) / s_k->attackS;
        const float needed = (float)((s_k->hp + s_stats.orbPower - 1) / s_stats.orbPower);
        s_huntT = needed / orbsPerS * BATTLE_HUNT_SLACK;
        s_laneT = 1.0f;
      }
      break;
    }

    case Phase::Hunt: {
      s_huntT -= dt;
      if (s_huntT <= 0.0f) {
        s_huntT = 0.0f;
        s_outcome = Outcome::GotAway;
        return;
      }
      s_laneT -= dt;
      if (s_laneT <= 0.0f) {                                 // pick a new lane
        float nl;
        do { nl = (float)((int)(rnd() % 3) - 1); } while (nl == s_laneTarget);
        s_laneTarget = nl;
        s_laneT = rndf(1.2f, 2.5f);
      }
      const float step = BATTLE_ENEMY_LANE_SPEED * dt;
      if (s_lane < s_laneTarget) s_lane = fminf(s_lane + step, s_laneTarget);
      else if (s_lane > s_laneTarget) s_lane = fmaxf(s_lane - step, s_laneTarget);

      s_attackT -= dt;
      if (s_attackT <= 0.0f && fabsf(s_lane - s_laneTarget) < 0.05f) {
        attack();
        s_attackT = s_k->attackS * rndf(0.85f, 1.15f);
      }
      break;
    }
  }
}

void onObstacleHit(bool wall) {
  char buf[32];
  if (s_phase == Phase::Pursuit) {
    const float loss = (wall ? BATTLE_GAP_LOSS_WALL : BATTLE_GAP_LOSS_BARRIER) * s_stats.gapLossMul;
    s_gap -= loss;
    s_gapShown = s_gap;                         // the pursuer lunges in at once
    snprintf(buf, sizeof buf, "STUMBLE!  -%d m", (int)loss);
    banner(buf, rgb565(255, 140, 60));
  } else if (s_phase == Phase::Hunt) {
    --s_hearts;
    banner(s_hearts > 0 ? "OUCH!  -1 HEART" : "DOWN!", rgb565(235, 60, 60));
    if (s_hearts <= 0) s_outcome = Outcome::Caught;
  }
}

void onShieldBlock() { banner("SHIELD!", rgb565(90, 200, 255)); }

void onOrb() {
  if (s_phase != Phase::Hunt || s_outcome != Outcome::None) return;
  s_hp -= s_stats.orbPower;
  s_flashT = 0.2f;
  banner(s_stats.orbPower > 1 ? "SURGE HIT!" : "HIT!", rgb565(120, 230, 255));
  if (s_hp <= 0) { s_hp = 0; s_outcome = Outcome::Defeated; }
}

Phase   phase()   { return s_phase; }
Outcome outcome() { return s_outcome; }
float   stumbleSeconds(bool wall) {
  return (wall ? BATTLE_STUMBLE_WALL_S : BATTLE_STUMBLE_S) * s_stats.stumbleMul;
}
float   huntSecondsLeft() { return s_huntT; }
const char* enemyName() { return s_k->name; }
bool  enemyOnRoad() { return s_phase != Phase::Pursuit; }
float enemyZAhead() { return s_zAhead; }
float enemyLane()   { return s_lane; }

void drawEnemyOnRoad(lgfx::LGFX_Sprite& b, int32_t bandY, int32_t x, int32_t y, float s,
                     float time) {
  if (s_blinkT > 0.0f && ((int32_t)(time * 30.0f) & 1)) return;     // phantom flicker
  const int32_t w = (int32_t)(110.0f * s) + 4, h = (int32_t)(140.0f * s) + 4;
  if (!hud::rowsHit(y - h - 20, y + 8, bandY, b.height())) return;
  const bool flash = s_flashT > 0.0f;
  const uint16_t body = flash ? rgb565(255, 255, 255) : s_k->body;
  const int32_t by = y - bandY;
  const bool stride = ((int32_t)(time * 8.0f) & 1) != 0;

  b.fillEllipse(x, by, w / 2, w / 8 + 1, rgb565(30, 30, 34));        // shadow
  switch (s_k->attack) {
    case Attack::Barrier: {                                          // Shade: ghost from behind
      b.fillCircle(x, by - h + w / 2, w / 2, body);
      b.fillRect(x - w / 2, by - h + w / 2, w, h - w / 2 - h / 8, body);
      for (int i = 0; i < 3; ++i) {                                  // wavy tail
        const int32_t tx = x - w / 2 + i * w / 3;
        b.fillTriangle(tx, by - h / 8, tx + w / 3, by - h / 8, tx + w / 6, by - (stride ? 0 : h / 16), body);
      }
      break;
    }
    case Attack::TwoWalls: {                                         // Brute: hulking back + horns
      b.fillRect(x - w / 3, by - h / 4, w / 5, h / 4, stride ? body : rgb565(120, 25, 25));   // legs
      b.fillRect(x + w / 8, by - h / 4, w / 5, h / 4, stride ? rgb565(120, 25, 25) : body);
      b.fillRoundRect(x - w / 2, by - h, w, h * 3 / 4, w / 6 + 1, body);
      b.fillTriangle(x - w / 2, by - h + h / 10, x - w / 3, by - h, x - w / 2 - w / 6, by - h - h / 6,
                     rgb565(240, 220, 180));
      b.fillTriangle(x + w / 2, by - h + h / 10, x + w / 3, by - h, x + w / 2 + w / 6, by - h - h / 6,
                     rgb565(240, 220, 180));
      break;
    }
    case Attack::PhaseWall: {                                        // Phantom: glowing wisp
      b.fillCircle(x, by - h / 2, w / 2, body);
      b.fillCircle(x - w / 6, by - h / 2 - w / 6, w / 5 + 1, s_k->accent);
      const int32_t sp = ((int32_t)(time * 10.0f)) % 6;
      b.fillRect(x + w / 2 + sp, by - h + sp * 3, 3, 3, rgb565(255, 255, 255));
      b.fillRect(x - w / 2 - sp, by - h / 3 - sp * 2, 3, 3, rgb565(255, 255, 255));
      break;
    }
  }
}

void drawPursuer(lgfx::LGFX_Sprite& b, int32_t bandY, int32_t runnerX, float time) {
  if (s_phase != Phase::Pursuit) return;
  float c = 1.0f - s_gapShown / BATTLE_GAP_VIS_M;
  if (c < 0.0f) c = 0.0f;
  if (c > 1.0f) c = 1.0f;
  const int32_t h = 26 + (int32_t)(92.0f * c);
  const int32_t w = 70 + (int32_t)(70.0f * c);
  const int32_t bob = (int32_t)(sinf(time * 9.0f) * 3.0f);
  // Beside the runner, on the side with more room, rising from the bottom edge.
  int32_t x = runnerX + (runnerX >= LCD_WIDTH / 2 ? -150 : 150);
  if (x < w / 2) x = w / 2;
  if (x > LCD_WIDTH - w / 2) x = LCD_WIDTH - w / 2;
  const int32_t top = LCD_HEIGHT - h + bob;

  if (c > 0.55f && ((int32_t)(time * 6.0f) & 1)) {                   // danger edges
    hud::rect(b, bandY, 0, 0, 6, LCD_HEIGHT, rgb565(200, 30, 30));
    hud::rect(b, bandY, LCD_WIDTH - 6, 0, 6, LCD_HEIGHT, rgb565(200, 30, 30));
  }
  if (!hud::rowsHit(top - 20, LCD_HEIGHT, bandY, b.height())) return;
  const int32_t ty = top - bandY;
  const uint16_t body = s_k->body;
  switch (s_k->attack) {
    case Attack::Barrier:                                            // Shade: dome + eyes + claws
      b.fillCircle(x, ty + w / 2, w / 2, body);
      b.fillRect(x - w / 2, ty + w / 2, w, LCD_HEIGHT - top, body);
      b.fillRect(x - w / 5 - w / 12, ty + w / 3, w / 8 + 1, w / 6 + 1, s_k->accent);
      b.fillRect(x + w / 5 - w / 24, ty + w / 3, w / 8 + 1, w / 6 + 1, s_k->accent);
      b.fillTriangle(x - w / 2, ty + w / 2, x - w / 2 - w / 4, ty + w / 3, x - w / 2, ty + w / 2 + w / 6, body);
      b.fillTriangle(x + w / 2, ty + w / 2, x + w / 2 + w / 4, ty + w / 3, x + w / 2, ty + w / 2 + w / 6, body);
      break;
    case Attack::TwoWalls:                                           // Brute: horned head
      b.fillRoundRect(x - w / 2, ty, w, LCD_HEIGHT - top + 10, w / 6 + 1, body);
      b.fillTriangle(x - w / 2, ty + w / 6, x - w / 3, ty, x - w / 2 - w / 5, ty - w / 4, rgb565(240, 220, 180));
      b.fillTriangle(x + w / 2, ty + w / 6, x + w / 3, ty, x + w / 2 + w / 5, ty - w / 4, rgb565(240, 220, 180));
      b.fillRect(x - w / 4, ty + w / 4, w / 7 + 1, w / 10 + 1, s_k->accent);
      b.fillRect(x + w / 4 - w / 7, ty + w / 4, w / 7 + 1, w / 10 + 1, s_k->accent);
      break;
    case Attack::PhaseWall:                                          // Phantom: big wisp
      b.fillCircle(x, ty + w / 2, w / 2, body);
      b.fillCircle(x - w / 6, ty + w / 3, w / 5 + 1, s_k->accent);
      b.fillRect(x - w / 2, ty + w / 2, w, LCD_HEIGHT - top, body);
      break;
  }
}

void composeHud(lgfx::LGFX_Sprite& b, int32_t bandY, float time) {
  char buf[32];
  const int32_t cx = LCD_WIDTH / 2;
  hud::text(b, bandY, s_k->name, cx, 34, hud::F9, 1.0f, rgb565(255, 90, 80));

  if (s_phase == Phase::Pursuit) {                                   // gap meter
    const float g = fmaxf(0.0f, fminf(s_gapShown, BATTLE_GAP_ESCAPE_M));
    const float f = g / BATTLE_GAP_ESCAPE_M;
    const uint16_t gc = f > 0.6f ? rgb565(60, 220, 90) : (f > 0.3f ? rgb565(255, 200, 40) : rgb565(235, 60, 50));
    hud::rect(b, bandY, cx - 80, 50, 160, 10, rgb565(32, 32, 36));
    hud::rect(b, bandY, cx - 78, 52, (int32_t)(156 * f), 6, gc);
    snprintf(buf, sizeof buf, "GAP %d m", (int)g);
    hud::text(b, bandY, buf, cx, 74, hud::F12, 1.0f, gc);
  } else {                                                           // hearts + enemy HP
    for (int i = 0; i < BATTLE_HEARTS + s_stats.extraHearts; ++i)
      heart(b, bandY, 12 + i * 20, 52, i < s_hearts);
    const float f = (float)s_hp / s_k->hp;
    hud::rect(b, bandY, cx - 90, 48, 180, 12, rgb565(32, 32, 36));
    hud::rect(b, bandY, cx - 88, 50, (int32_t)(176 * f), 8, rgb565(235, 60, 60));
    snprintf(buf, sizeof buf, "HP %d / %d", s_hp, s_k->hp);
    hud::text(b, bandY, buf, cx, 72, hud::F9, 1.0f, rgb565(255, 255, 255));
    if (s_phase == Phase::Hunt) {                                    // time limit
      const int secs = (int)ceilf(s_huntT);
      const bool warn = s_huntT <= BATTLE_HUNT_WARN_S;
      if (!warn || ((int32_t)(time * 4.0f) & 1)) {
        snprintf(buf, sizeof buf, "%d:%02d", secs / 60, secs % 60);
        hud::text(b, bandY, buf, cx + 130, 54, hud::F18, 1.0f,
                  warn ? rgb565(255, 60, 50) : rgb565(255, 255, 255));
      }
    }
  }

  if (s_bannerT > 0.0f) hud::text(b, bandY, s_banner, cx, 110, hud::F18, 1.0f, s_bannerColor);
}

} // namespace battle
