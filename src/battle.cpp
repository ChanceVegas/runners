// battle.cpp — see battle.h. All per-kind behaviour (HP, attack rhythm, attack shape,
// orb drop rate, gap gain) lives in the KINDS table so enemies differ by data.
#include "battle.h"
#include "encounter.h"
#include "audio.h"
#include "sprite.h"
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

enum class Attack : uint8_t { Barrier, Smash, Phase, Stalk, Combo, Wall2 };   // per kind (world order)
using encounter::Block;

struct KindDef {
  const char* name;
  uint8_t hp;
  float attackS;
  int orbPct;
  Attack attack;
  uint16_t body, accent;
  float gapPen;        // m/s less Pursuit gap gain (Stalker)
  float rowMul;        // x Pursuit row spacing (Hornet: denser)
  uint8_t hitHearts;   // hearts lost per Hunt hit (Warden: 2)
};

const KindDef KINDS[6] = {
  { "SHADE",   BATTLE_SHADE_HP,   BATTLE_SHADE_ATTACK_S,   BATTLE_SHADE_ORB_PCT,   Attack::Barrier,
    rgb565(120, 60, 170), rgb565(255, 255, 255), 0.0f, 1.0f, 1 },
  { "BRUTE",   BATTLE_BRUTE_HP,   BATTLE_BRUTE_ATTACK_S,   BATTLE_BRUTE_ORB_PCT,   Attack::Smash,
    rgb565(180, 40, 40),  rgb565(255, 220, 0), 0.0f, 1.0f, 1 },
  { "PHANTOM", BATTLE_PHANTOM_HP, BATTLE_PHANTOM_ATTACK_S, BATTLE_PHANTOM_ORB_PCT, Attack::Phase,
    rgb565(60, 190, 220), rgb565(220, 250, 255), 0.0f, 1.0f, 1 },
  { "STALKER", BATTLE_STALKER_HP, BATTLE_STALKER_ATTACK_S, BATTLE_STALKER_ORB_PCT, Attack::Stalk,
    rgb565(52, 140, 96), rgb565(222, 40, 40), BATTLE_STALKER_GAP_PEN, 1.0f, 1 },
  { "HORNET",  BATTLE_HORNET_HP,  BATTLE_HORNET_ATTACK_S,  BATTLE_HORNET_ORB_PCT,  Attack::Combo,
    rgb565(255, 210, 0), rgb565(24, 24, 24), 0.0f, BATTLE_HORNET_ROW_MUL, 1 },
  { "WARDEN",  BATTLE_WARDEN_HP,  BATTLE_WARDEN_ATTACK_S,  BATTLE_WARDEN_ORB_PCT,  Attack::Wall2,
    rgb565(84, 86, 96), rgb565(240, 180, 20), 0.0f, 1.0f, BATTLE_WARDEN_HIT_HEARTS },
};
float s_playerLane = 0.0f, s_playerSpeed = 20.0f;   // from encounter (notePlayer)

const KindDef* s_k = &KINDS[0];
uint8_t s_level = 1;
float   s_goalM = 600.0f;
battle::Stats s_stats;
uint32_t s_xp = 0;                   // player's XP when the battle started
uint8_t  s_plvl = 1;                 // player level
float    s_diff = 0.0f;              // difficulty units from the player level (config DIFF_*)
int      s_hpMax = 5;
float    s_attackS = 1.6f;           // this battle's attack interval (level-scaled)
int      s_orbPct = 70;

Phase   s_phase = Phase::Pursuit;
Outcome s_outcome = Outcome::None;
float s_gap = BATTLE_GAP_START_M, s_gapShown = BATTLE_GAP_START_M;
int   s_hearts = BATTLE_HEARTS;
int   s_hp = 5;
float s_travel = 0.0f, s_playerZ = 6.0f;

float s_overT = 0.0f;
battle::Start s_start = battle::Start::Behind;
bool  s_startBanner = false;         // show the BESIDE/AHEAD banner once the run starts (not over the countdown)
float s_zAhead = 0.0f;
float s_passT = 0.0f;                // DEFEATED: s into the "run past it" animation
bool  s_lastOrb = true;              // orb pity: never two attacks in a row without one
bool  s_passed = false;              // passed it once: DEFEATED locked in, enemy weakened
float s_runM = 0.0f;                 // metres since the battle started (last update)
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
  const float g = BATTLE_GAP_GAIN_L1 - (s_level - 1) * BATTLE_GAP_GAIN_STEP
                  - s_diff * DIFF_GAP_GAIN_STEP - s_k->gapPen + s_stats.gapGainBonus;
  const float w = g + (s_passed ? BATTLE_PASS_GAIN_BONUS : 0.0f);
  return w > DIFF_GAP_GAIN_MIN ? w : DIFF_GAP_GAIN_MIN;
}

void beginOvertake() {
  s_phase = Phase::Overtake;
  s_overT = 0.0f;
  s_zAhead = 0.5f;
  s_lane = s_laneTarget = 0.0f;
  if (s_passed && s_hp <= 0) s_hp = (s_hpMax + 1) / 2;    // re-caught a passed enemy: half HP
  encounter::engine::clearAhead();
  banner("IT'S AHEAD - CATCH IT!", rgb565(255, 220, 40));
}

// Overtake finished (or an AHEAD start): the Hunt begins with its time limit.
void enterHunt() {
  s_phase = Phase::Hunt;
  s_attackT = 0.8f;
  // Time limit from the fastest possible kill: every orb collected, every attack
  // dropping one at the kind's orb rate.
  const float orbsPerS = (s_orbPct / 100.0f) / s_attackS;
  const float needed = (float)((s_hpMax + s_stats.orbPower - 1) / s_stats.orbPower);
  s_huntT = needed / orbsPerS * BATTLE_HUNT_SLACK;
  s_laneT = 1.0f;
}

void attack() {
  const int8_t el = (int8_t)lroundf(s_lane);
  // Just behind the enemy, but never closer than BATTLE_ATTACK_MIN_Z_M (when the enemy
  // is near, the attack lands just past it and the player still gets time to react).
  const float wz = s_travel + s_playerZ + fmaxf(s_zAhead - 1.5f, BATTLE_ATTACK_MIN_Z_M);
  int8_t blocked[3] = {0, 0, 0};
  switch (s_k->attack) {
    case Attack::Barrier:                                   // Shade: barrier in its lane
      encounter::engine::spawnObstacle(Block::Barrier, el, wz);
      blocked[el + 1] = 1;
      break;
    case Attack::Smash: {                                   // Brute: duck bars, 2 lanes or all 3
      if (rnd() % 3 == 0) {                                 // full-width smash: must duck
        for (int8_t l = -1; l <= 1; ++l) encounter::engine::spawnObstacle(Block::Overhead, l, wz);
        blocked[0] = blocked[1] = blocked[2] = 1;
      } else {
        int8_t other = (el == 0) ? (int8_t)((rnd() & 1) ? 1 : -1) : 0;
        encounter::engine::spawnObstacle(Block::Overhead, el, wz);
        encounter::engine::spawnObstacle(Block::Overhead, other, wz);
        blocked[el + 1] = blocked[other + 1] = 1;
      }
      break;
    }
    case Attack::Stalk: {                                   // Stalker: wall in YOUR lane + barrier beside
      const int8_t other = (el == 0) ? (int8_t)((rnd() & 1) ? 1 : -1) : 0;
      encounter::engine::spawnObstacle(Block::Wall, el, wz);
      encounter::engine::spawnObstacle(Block::Barrier, other, wz);
      blocked[el + 1] = blocked[other + 1] = 1;
      break;
    }
    case Attack::Combo: {                                   // Hornet: jump, then duck, same lane
      const float gap = fmaxf(BATTLE_HORNET_COMBO_MIN_M, s_playerSpeed * BATTLE_HORNET_COMBO_S);
      encounter::engine::spawnObstacle(Block::Barrier, el, wz);
      encounter::engine::spawnObstacle(Block::Overhead, el, wz + gap);
      blocked[el + 1] = 1;
      break;
    }
    case Attack::Wall2: {                                   // Warden: walls across two lanes
      const int8_t other = (el == 0) ? (int8_t)((rnd() & 1) ? 1 : -1) : 0;
      encounter::engine::spawnObstacle(Block::Wall, el, wz);
      encounter::engine::spawnObstacle(Block::Wall, other, wz);
      blocked[el + 1] = blocked[other + 1] = 1;
      break;
    }
    case Attack::Phase: {                                   // Phantom: random kind, then blink away
      static const Block KINDS_P[3] = { Block::Barrier, Block::Overhead, Block::Wall };
      encounter::engine::spawnObstacle(KINDS_P[rnd() % 3], el, wz);
      blocked[el + 1] = 1;
      int8_t nl;
      do { nl = (int8_t)((int)(rnd() % 3) - 1); } while (nl == el);
      s_lane = s_laneTarget = nl;
      s_blinkT = 0.25f;
      break;
    }
  }
  // Orb in a free lane of the same row, so it never sits inside an obstacle.
  const bool orb = !s_lastOrb || (int)(rnd() % 100) < s_orbPct;
  s_lastOrb = orb;
  if (orb) {
    int8_t free[3]; int nf = 0;
    for (int8_t l = -1; l <= 1; ++l) if (!blocked[l + 1]) free[nf++] = l;
    if (nf) encounter::engine::spawnOrb(free[rnd() % nf], wz);
    else encounter::engine::spawnOrb((int8_t)((int)(rnd() % 3) - 1), wz - 6.0f);   // full smash: orb just before it
  }
}

// Heart icon at (x, y) top-left, ~16x14.
// Heart icon at (x, y) top-left, 18x16 (A2 art at 2x).
void heart(lgfx::LGFX_Sprite& b, int32_t bandY, int32_t x, int32_t y, bool full) {
  sprite::draw(b, bandY, full ? ART_HEART : ART_HEART_EMPTY, x, y, 18, 16);
}

// Per-kind art (A2): back views (2 run frames) and the front view.
const ArtSprite& backArt(Attack a, bool stride) {
  switch (a) {
    case Attack::Barrier: return stride ? ART_SHADE_B1 : ART_SHADE_B0;
    case Attack::Smash:   return stride ? ART_BRUTE_B1 : ART_BRUTE_B0;
    case Attack::Stalk:   return stride ? ART_STALKER_B1 : ART_STALKER_B0;
    case Attack::Combo:   return stride ? ART_HORNET_B1 : ART_HORNET_B0;
    case Attack::Wall2:   return stride ? ART_WARDEN_B1 : ART_WARDEN_B0;
    default:              return stride ? ART_PHANTOM_B1 : ART_PHANTOM_B0;
  }
}
const ArtSprite& frontArt(Attack a) {
  switch (a) {
    case Attack::Barrier: return ART_SHADE_F;
    case Attack::Smash:   return ART_BRUTE_F;
    case Attack::Stalk:   return ART_STALKER_F;
    case Attack::Combo:   return ART_HORNET_F;
    case Attack::Wall2:   return ART_WARDEN_F;
    default:              return ART_PHANTOM_F;
  }
}

} // namespace

namespace battle {

uint32_t xpToReach(uint8_t level) {
  // XP from L to L+1 = BASE + STEP*L, so total to reach level n = sum over L=1..n-1.
  if (level <= 1) return 0;
  const uint32_t n = level - 1;
  return n * LEVEL_XP_BASE + LEVEL_XP_STEP * n * (n + 1) / 2;
}

uint8_t levelOf(uint32_t xp) {
  uint8_t l = 1;
  while (l < LEVEL_MAX && xp >= xpToReach(l + 1)) l++;
  return l;
}

uint16_t xpGain(uint8_t kind, uint8_t enemyLevel, Outcome o) {
  int base = o == Outcome::Escaped ? XP_ESCAPE : o == Outcome::Defeated ? XP_DEFEAT
           : o == Outcome::GotAway ? XP_GOTAWAY : o == Outcome::Caught ? XP_CAUGHT : 0;
  static const int PCT[6] = { XP_SHADE_PCT, XP_BRUTE_PCT, XP_PHANTOM_PCT,
                              XP_STALKER_PCT, XP_HORNET_PCT, XP_WARDEN_PCT };
  const int typePct = PCT[kind < 6 ? kind : 0];
  const int lvlPct = 100 + (enemyLevel > 1 ? (enemyLevel - 1) : 0) * XP_LEVEL_STEP_PCT;
  return (uint16_t)((base * typePct * lvlPct + 5000) / 10000);
}

uint16_t xpGainNow(Outcome o) { return xpGain((uint8_t)(s_k - KINDS), s_level, o); }

float diffOf(uint8_t playerLevel) {
  return (playerLevel - 1) * DIFF_AT_MAX / (float)(LEVEL_MAX - 1);
}
float rewardMul(uint8_t l) { return 1.0f + diffOf(l) * DIFF_REWARD_STEP; }
uint8_t playerLevel() { return s_plvl; }
uint32_t startXp() { return s_xp; }
float speedBonus() { return s_diff * DIFF_SPEED_STEP; }
float rowGapMul() { return fmaxf(0.7f, 1.0f - s_diff * DIFF_ROW_GAP_STEP) * s_k->rowMul; }
void  notePlayer(float lane, float speedMS) { s_playerLane = lane; s_playerSpeed = speedMS; }

void start(uint8_t kind, uint8_t level, float goalM, const Stats& stats, uint32_t xp) {
  s_k = &KINDS[kind < 6 ? kind : 0];
  s_level = level;
  s_goalM = goalM;
  s_stats = stats;
  s_rng = esp_random() | 1u;
  s_phase = Phase::Pursuit;
  s_outcome = Outcome::None;
  s_gap = s_gapShown = BATTLE_GAP_START_M + stats.startGapBonus;
  s_hearts = BATTLE_HEARTS + stats.extraHearts;
  s_xp = xp;
  s_plvl = levelOf(xp);
  s_diff = diffOf(s_plvl);
  s_hpMax = s_k->hp + (int)(s_diff / DIFF_HP_EVERY);
  s_hp = s_hpMax;
  s_attackS = s_k->attackS * fmaxf(0.65f, 1.0f - s_diff * DIFF_ATTACK_STEP);
  s_orbPct = s_k->orbPct - (int)(s_diff * DIFF_ORB_STEP);
  if (s_orbPct < 35) s_orbPct = 35;
  s_huntT = 0.0f;
  s_zAhead = 0.0f;
  s_passT = 0.0f;
  s_lastOrb = true;
  s_passed = false;
  s_runM = 0.0f;
  s_flashT = s_blinkT = 0.0f;
  s_bannerT = 0.0f;
  s_lane = s_laneTarget = 0.0f;

  // L1b: start position by player level. RUSH (startGapBonus) still widens a BESIDE gap;
  // it does nothing for an AHEAD start (user: leave it for now).
  s_start = s_plvl >= BATTLE_START_AHEAD_LV ? Start::Ahead
          : s_plvl >= BATTLE_START_BESIDE_LV ? Start::Beside : Start::Behind;
#if DEBUG_START_CYCLE
  static uint8_t s_cycle = 0;
  s_start = (Start)(s_cycle++ % 3);
#endif
  if (s_start == Start::Beside) {
    s_gap = s_gapShown = BATTLE_GAP_BESIDE_M + stats.startGapBonus;
  } else if (s_start == Start::Ahead) {
    s_zAhead = BATTLE_HUNT_Z_M;                // already out in front: no Pursuit at all
    enterHunt();
  }
  s_startBanner = s_start != Start::Behind;
  Serial.printf("[battle] start %s (player L%u)\n",
                s_start == Start::Ahead ? "AHEAD" : s_start == Start::Beside ? "BESIDE" : "BEHIND",
                (unsigned)s_plvl);
}

void update(float dt, float travel, float runM, float playerZ, bool stumbling) {
  s_travel = travel;
  s_playerZ = playerZ;
  s_runM = runM;
  if (s_startBanner) {                                     // first run frame after the countdown
    s_startBanner = false;
    if (s_start == Start::Beside) banner("NECK AND NECK - RUN!", rgb565(255, 160, 40));
    else banner("IT'S AHEAD - CATCH IT!", rgb565(255, 220, 40));
  }
  if (s_bannerT > 0) s_bannerT -= dt;
  if (s_flashT > 0) s_flashT -= dt;
  if (s_blinkT > 0) s_blinkT -= dt;
  if (s_outcome != Outcome::None) return;

  if (s_passT > 0.0f) {                                    // caught it: run past, then win
    s_passT += dt;
    const float t = fminf(s_passT / BATTLE_PASS_S, 1.0f);
    s_zAhead = BATTLE_HUNT_Z_LAST_M * 0.6f * (1.0f - t);   // to 0 = level with you
    if (t >= 1.0f) {                                       // passed it: race again
      s_passT = 0.0f;
      s_passed = true;
      s_phase = Phase::Pursuit;
      s_gap = s_gapShown = BATTLE_PASS_GAP_M;
      s_goalM = runM + BATTLE_PASS_ESCAPE_M;
      encounter::engine::clearAhead();
      encounter::engine::restartRace(s_goalM);
      char buf[32];
      snprintf(buf, sizeof buf, "YOU PASSED THE %s!", s_k->name);
      banner(buf, rgb565(255, 220, 40));
      s_bannerT = BATTLE_PASS_BANNER_S;
      audio::play(audio::Sfx::Win);
      Serial.printf("[battle] passed the %s at %d m; escape by %d m\n", s_k->name, (int)runM,
                    (int)s_goalM);
    }
    return;
  }

  switch (s_phase) {
    case Phase::Pursuit:
      if (!stumbling) s_gap += gapGain() * dt;
      s_gapShown += (s_gap - s_gapShown) * fminf(1.0f, dt * 5.0f);
      if (s_gap >= BATTLE_GAP_ESCAPE_M || runM >= s_goalM) {
        s_outcome = s_passed ? Outcome::Defeated : Outcome::Escaped;   // passed + got away = beaten
        return;
      }
      if (s_gap <= 0.0f) beginOvertake();
      break;

    case Phase::Overtake: {
      s_overT += dt;
      const float t = fminf(s_overT / BATTLE_OVERTAKE_S, 1.0f);
      const float e = t * t * (3.0f - 2.0f * t);
      s_zAhead = 0.5f + (BATTLE_HUNT_Z_M - 0.5f) * e;
      if (t >= 1.0f) enterHunt();
      break;
    }

    case Phase::Hunt: {
      {                                                     // distance follows HP
        const float hpF = s_hpMax > 1 ? (float)(s_hp - 1) / (float)(s_hpMax - 1) : 1.0f;
        const float target = BATTLE_HUNT_Z_LAST_M + (BATTLE_HUNT_Z_M - BATTLE_HUNT_Z_LAST_M) * hpF;
        const float step = BATTLE_HUNT_CLOSE_MS * dt;
        if (s_zAhead > target) s_zAhead = fmaxf(s_zAhead - step, target);
        else if (s_zAhead < target) s_zAhead = fminf(s_zAhead + step, target);
      }
      s_huntT -= dt;
      if (s_huntT <= 0.0f) {
        s_huntT = 0.0f;
        s_outcome = Outcome::GotAway;
        return;
      }
      s_laneT -= dt;
      if (s_laneT <= 0.0f) {                                 // pick a new lane
        if (s_k->attack == Attack::Stalk) {                  // Stalker: aim at YOUR lane
          s_laneTarget = (float)lroundf(s_playerLane);
          s_laneT = BATTLE_STALKER_LANE_S;
        } else {
          float nl;
          do { nl = (float)((int)(rnd() % 3) - 1); } while (nl == s_laneTarget);
          s_laneTarget = nl;
          s_laneT = rndf(1.2f, 2.5f);
        }
      }
      const float step = BATTLE_ENEMY_LANE_SPEED * dt;
      if (s_lane < s_laneTarget) s_lane = fminf(s_lane + step, s_laneTarget);
      else if (s_lane > s_laneTarget) s_lane = fmaxf(s_lane - step, s_laneTarget);

      s_attackT -= dt;
      if (s_attackT <= 0.0f && fabsf(s_lane - s_laneTarget) < 0.05f) {
        attack();
        s_attackT = s_attackS * rndf(0.85f, 1.15f);
        if (s_k->attack == Attack::Combo) s_attackT += BATTLE_HORNET_COMBO_S;   // pair stays clear
      }
      break;
    }
  }
}

void onObstacleHit(bool wall) {
  if (s_passT > 0.0f) return;                 // already caught it: nothing can hurt now
  char buf[32];
  if (s_phase == Phase::Pursuit) {
    const float loss = (wall ? BATTLE_GAP_LOSS_WALL : BATTLE_GAP_LOSS_BARRIER) * s_stats.gapLossMul *
                       (1.0f + s_diff * DIFF_GAP_LOSS_STEP);
    s_gap -= loss;
    s_gapShown = s_gap;                         // the pursuer lunges in at once
    snprintf(buf, sizeof buf, "STUMBLE!  -%d m", (int)loss);
    banner(buf, rgb565(255, 140, 60));
  } else if (s_phase == Phase::Hunt) {
    s_hearts -= s_k->hitHearts;
    if (s_hearts < 0) s_hearts = 0;
    banner(s_hearts <= 0 ? "DOWN!" : (s_k->hitHearts > 1 ? "CRUSHED!  -2 HEARTS" : "OUCH!  -1 HEART"),
           rgb565(235, 60, 60));
    if (s_hearts <= 0) s_outcome = Outcome::Caught;
  }
}

void onShieldBlock() { banner("SHIELD!", rgb565(90, 200, 255)); }

void onOrb() {
  if (s_phase != Phase::Hunt || s_outcome != Outcome::None) return;
  s_hp -= s_stats.orbPower;
  s_flashT = 0.2f;
  if (s_hp <= 0) {                                          // caught: run past it
    s_hp = 0;
    s_passT = 0.001f;
    banner("CAUGHT IT!", rgb565(255, 220, 40));
  } else {
    banner(s_stats.orbPower > 1 ? "SURGE HIT! CLOSING IN" : "HIT! CLOSING IN", rgb565(120, 230, 255));
  }
  Serial.printf("[battle] orb hit: HP %d/%d, %.1f s left\n", s_hp, s_hpMax, s_huntT);
}

Phase   phase()   { return s_phase; }
Outcome outcome() { return s_outcome; }
float   stumbleSeconds(bool wall) {
  return (wall ? BATTLE_STUMBLE_WALL_S : BATTLE_STUMBLE_S) * s_stats.stumbleMul;
}
float   huntSecondsLeft() { return s_huntT; }
const char* enemyName() { return s_k->name; }
Start   startPos()  { return s_start; }
bool  passing() { return s_passT > 0.0f; }
bool  passedIt() { return s_passed; }
// Hidden once you draw level during the pass (it's "behind" you from then on).
bool  enemyOnRoad() { return s_phase != Phase::Pursuit && !(s_passT > 0.0f && s_zAhead < 1.5f); }
float enemyZAhead() { return s_zAhead; }
float enemyLane()   { return s_lane; }

void drawEnemyOnRoad(lgfx::LGFX_Sprite& b, int32_t bandY, int32_t x, int32_t y, float s,
                     float time) {
  if (s_blinkT > 0.0f && ((int32_t)(time * 30.0f) & 1)) return;     // phantom flicker
  const ArtSprite& a = backArt(s_k->attack, ((int32_t)(time * 8.0f) & 1) != 0);
  const int32_t w = (int32_t)(110.0f * s) + 4, h = w * a.h / a.w;
  if (!hud::rowsHit(y - h, y + 8, bandY, b.height())) return;
  if (hud::rowsHit(y - w / 8 - 1, y + w / 8 + 2, bandY, b.height()))
    b.fillEllipse(x, y - bandY, w / 2, w / 8 + 1, rgb565(30, 30, 34));   // shadow
  sprite::draw(b, bandY, a, x - w / 2, y - h, w, h, false,
               s_flashT > 0.0f ? rgb565(255, 255, 255) : 0);             // white on a hit
}

void drawPursuer(lgfx::LGFX_Sprite& b, int32_t bandY, int32_t runnerX, float time) {
  if (s_phase != Phase::Pursuit) return;
  float c = 1.0f - s_gapShown / BATTLE_GAP_VIS_M;
  if (c < 0.0f) c = 0.0f;
  if (c > 1.0f) c = 1.0f;
  const int32_t h = 26 + (int32_t)(92.0f * c);        // visible height above the bottom edge
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
  // Front view (A2), full sprite drawn from `top`; the part below the screen is clipped,
  // so the on-screen footprint is the same w x h as the B1 shapes.
  const ArtSprite& a = frontArt(s_k->attack);
  sprite::draw(b, bandY, a, x - w / 2, top, w, w * a.h / a.w, runnerX >= LCD_WIDTH / 2);
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
    const float f = (float)s_hp / s_hpMax;
    hud::rect(b, bandY, cx - 90, 48, 180, 12, rgb565(32, 32, 36));
    hud::rect(b, bandY, cx - 88, 50, (int32_t)(176 * f), 8, rgb565(235, 60, 60));
    snprintf(buf, sizeof buf, "HP %d / %d", s_hp, s_hpMax);
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
