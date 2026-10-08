// encounter.h — the lane-chase game mode: runner, obstacle rows, coins, collision,
// stages, score + saved best, and the title -> countdown -> run -> crash/clear loop.
// Owns all encounter state; draws objects and HUD on top of lanes + scenery.
#pragma once
#include <LovyanGFX.hpp>
#include <stdint.h>
#include "battle.h"

namespace encounter {

enum class State : uint8_t { Ready, Countdown, Run, Crash, StageClear, GameOver };
enum class Mode : uint8_t { Arcade, Battle };
// Obstacle types: Barrier = low, jump over; Wall = tall, change lane;
// Overhead = duck bar (beam on posts), duck under (B1-R3).
enum class Block : uint8_t { Barrier, Wall, Overhead };

struct Result {
  bool won;            // battle: escaped or defeated the enemy
  bool defeated;       // battle: defeated it (bigger reward)
  bool gotAway;        // battle: Hunt timer ran out — enemy escaped, coins forfeited
  uint32_t coins;      // coins collected this encounter
  uint8_t shieldsUsed; // battle: shields broken
  bool exitToMenu;     // arcade: player tapped MENU on game over
};

bool init();                    // seed RNG, load best score from flash, enter Ready

// Ready = idle attract road (the menu draws on top). Starting a mode runs the
// countdown. Arcade: endless stages, best score, MENU button on game over.
// Battle: pursuit -> (if overtaken) hunt vs an overworld enemy (rules in battle.*);
// `goalM` metres = escape distance, `level` = speed tier, `shields` absorb hits.
void startArcade();
void startBattle(uint8_t kind, uint8_t level, float goalM, uint8_t shields,
                 const battle::Stats& stats);
bool finished();                // chase over, or arcade MENU tapped — read result()
Result result();
void idle();                    // back to Ready (attract road), clears the road
uint32_t bestScore();           // arcade best (saved in flash)

// Engine hooks for battle.cpp: spawn things on the road / clear it.
namespace engine {
void spawnObstacle(Block kind, int8_t lane, float wz);  // wz = world metres along the road
void spawnOrb(int8_t lane, float wz);
void clearAhead();                                       // remove everything ahead of the runner
}
void update(float dt);          // one fixed logic tick; reads input::state()

// Latch interpolated positions for this frame (alpha = fraction of a tick since
// the last update), hand the road/scenery their travel, and build the depth-sorted
// draw list. Call once before renderFrame.
void beginRender(float alpha);

void composeObjects(lgfx::LGFX_Sprite& band, int32_t bandY);  // coins, obstacles, runner
void composeHud(lgfx::LGFX_Sprite& band, int32_t bandY);      // score, titles, hints

State    state();
float    distanceM();           // metres run in the current stage
float    speedMS();             // current speed, m/s
uint32_t score();               // session score: metres (all stages) + coins x points
uint8_t  stage();               // 1-based

}
