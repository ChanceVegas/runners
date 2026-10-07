// encounter.h — the lane-chase game mode: runner, obstacle rows, coins, collision,
// stages, score + saved best, and the title -> countdown -> run -> crash/clear loop.
// Owns all encounter state; draws objects and HUD on top of lanes + scenery.
#pragma once
#include <LovyanGFX.hpp>
#include <stdint.h>

namespace encounter {

enum class State : uint8_t { Ready, Countdown, Run, Crash, StageClear, GameOver };

bool init();                    // seed RNG, load best score from flash, enter Ready
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
