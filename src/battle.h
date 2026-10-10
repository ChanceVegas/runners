// battle.h — rules for an overworld battle, layered on the encounter engine:
// Phase 1 PURSUIT (enemy behind, gap meter, stumbles cost ground) -> if the gap hits
// zero the enemy OVERTAKES -> Phase 2 HUNT (enemy ahead attacks with its own obstacle
// pattern, orbs strike it, hearts are your life; if its timer runs out the enemy
// GETS AWAY). encounter.cpp calls the hooks below;
// battle asks encounter to spawn things via encounter::engine::*.
#pragma once
#include <LovyanGFX.hpp>
#include <stdint.h>

namespace battle {

enum class Phase : uint8_t { Pursuit, Overtake, Hunt };
enum class Start : uint8_t { Behind, Beside, Ahead };   // L1b: by player level
enum class Outcome : uint8_t { None, Escaped, Defeated, Caught, GotAway };   // GotAway = Hunt timer ran out

// Player modifiers (sneakers / drinks from the shop; defaults = no upgrades).
struct Stats {
  float gapGainBonus = 0.0f;   // m/s added to clean-running gap gain (SPRINT)
  float gapLossMul   = 1.0f;   // multiplier on stumble gap loss (GRIP)
  float stumbleMul   = 1.0f;   // multiplier on stumble duration (GRIP)
  uint8_t extraHearts = 0;     // hearts added in the Hunt (GRIP tier 3)
  uint8_t orbPower   = 1;      // HP removed per orb (SURGE drink: 2)
  float startGapBonus = 0.0f;  // m added to the starting gap (RUSH drink)
  float jumpMul      = 1.0f;   // jump launch speed multiplier (SPRING); used by encounter
};

// kind: world::EnemyKind order (Shade, Brute, Phantom, Stalker, Hornet, Warden). level 1..3.
// xp: the player's total XP; the player's level scales the enemy (config DIFF_*).
void start(uint8_t kind, uint8_t level, float goalM, const Stats& stats, uint32_t xp);

// Runner level (L1): pure helpers shared by game_state (saving) and encounter (end screen).
uint8_t  levelOf(uint32_t xp);                     // 1..LEVEL_MAX
uint32_t xpToReach(uint8_t level);                 // total XP at which a level starts
uint16_t xpGain(uint8_t kind, uint8_t enemyLevel, Outcome o);   // XP for one battle (never < 0)
uint16_t xpGainNow(Outcome o);                     // xpGain for the current battle's enemy
float    diffOf(uint8_t playerLevel);              // difficulty units d at a level (0..DIFF_AT_MAX)
float    rewardMul(uint8_t playerLevel);           // win-bonus multiplier at a level
uint8_t  playerLevel();                            // this battle's player level
uint32_t startXp();                                // this battle's starting XP
// The runner's lane (-1..1) and road speed (m/s), set by encounter every frame before
// update(): the Stalker aims at your lane, the Hornet spaces its combo by your speed.
void     notePlayer(float lane, float speedMS);
float    speedBonus();                             // m/s added to the battle road speed
float    rowGapMul();                              // multiplier on Pursuit row spacing

// One logic tick while running. travel = metres run (absolute); runM = metres since
// the battle started; playerZ = runner depth (m); stumbling = runner is stumbling.
void update(float dt, float travel, float runM, float playerZ, bool stumbling);

void onObstacleHit(bool wall);     // a hit the runner took (no shield): ground / heart
void onShieldBlock();              // a shield absorbed a hit (banner only)
void onOrb();                      // an orb was collected

Phase   phase();
Outcome outcome();
float   stumbleSeconds(bool wall);  // stumble length for this hit (stats applied)
float   huntSecondsLeft();          // Hunt time limit remaining (s)
const char* enemyName();
Start   startPos();                                // where this battle's enemy started

// Hunt/overtake: enemy depth ahead of the runner (m), its lane, and whether it should
// be drawn in the road's depth-sorted list this frame.
bool  enemyOnRoad();
bool  passing();                   // HP hit 0: running past it (no collisions)
bool  passedIt();                  // passed at least once this battle = DEFEATED is locked in
float enemyZAhead();
float enemyLane();

// Drawing. drawEnemyOnRoad: at projected screen pos (x = centre, y = ground, s =
// scale). drawPursuer: Pursuit overlay at the bottom of the screen (behind runner).
void drawEnemyOnRoad(lgfx::LGFX_Sprite& b, int32_t bandY, int32_t x, int32_t y, float s,
                     float time);
void drawPursuer(lgfx::LGFX_Sprite& b, int32_t bandY, int32_t runnerX, float time);
void composeHud(lgfx::LGFX_Sprite& b, int32_t bandY, float time);

}
