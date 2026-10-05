// encounter.h — the lane-chase game mode: runner, obstacle rows, collision, the
// escape goal, and the READY -> RUN -> WIN/LOSE -> retry loop. Owns all encounter
// state; draws objects and HUD on top of lanes::composeBand.
#pragma once
#include <LovyanGFX.hpp>
#include <stdint.h>

namespace encounter {

enum class State : uint8_t { Ready, Run, Win, Lose };

bool init();                    // seed RNG, enter Ready
void update(float dt);          // one fixed logic tick; reads input::state()

// Latch interpolated positions for this frame (alpha = fraction of a tick since
// the last update) and hand the road its travel. Call once before renderFrame.
void beginRender(float alpha);

void composeObjects(lgfx::LGFX_Sprite& band, int32_t bandY);  // obstacles + runner
void composeHud(lgfx::LGFX_Sprite& band, int32_t bandY);      // progress, text

State state();
float distanceM();              // metres run this attempt
float speedMS();                // current speed, m/s

}
