// overworld.h — the top-down map mode: terrain around the player (from world::), the
// player avatar, nearby enemies, Run energy, and the engage rule (walk up to an enemy,
// then STOP to start the chase). Rendering: terrain written straight into the band
// buffer per row (fast path); enemies/avatar/HUD via library primitives.
#pragma once
#include <LovyanGFX.hpp>
#include <stdint.h>
#include "world.h"

namespace overworld {

bool init();

// One logic tick. Call after locator::update(). Reads input for the MENU button.
void update(float dt);

// Latch the interpolated player position for this frame + rebuild the tile cache if
// the view moved to new tiles. Call once before renderFrame.
void beginRender(float alpha);

void composeTerrain(lgfx::LGFX_Sprite& band, int32_t bandY);
void composeEntities(lgfx::LGFX_Sprite& band, int32_t bandY);
void composeHud(lgfx::LGFX_Sprite& band, int32_t bandY);

// Engagement handshake with game_state: true once when the player has stopped next
// to an enemy long enough; fills `out` with that enemy.
bool takeEngagement(world::Enemy& out);
void requireMoveBeforeEngage();     // after a chase: don't re-trigger until they move
bool takeMenuRequest();             // MENU button tapped

// Profile values game_state persists.
float energy();
void  setEnergy(float e);
float walkedM();                    // real-world metres walked/run (all time)
void  setWalkedM(float m);
void  setStats(uint32_t wallet, uint32_t escapes);   // for the HUD

}
