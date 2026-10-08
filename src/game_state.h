// game_state.h — top-level mode machine: Menu -> Explore (overworld) <-> Battle
// (encounter vs an enemy), or Menu -> Arcade (endless encounter). Owns the render
// layer stack for each mode, the battle handshake (shields from Run energy, rewards
// back), and the player profile saved in flash (coins, escapes, energy, position).
#pragma once
#include <LovyanGFX.hpp>
#include <stdint.h>

namespace game_state {

enum class Mode : uint8_t { Menu, Explore, Battle, Arcade };

bool init();                     // load profile, place the player, enter Menu
void update(float dt);           // one logic tick (after input::update)
void beginRender(float alpha);   // latch the active mode's frame, before renderFrame

Mode mode();
const char* modeName();
void composeMenu(lgfx::LGFX_Sprite& band, int32_t bandY);   // menu overlay layer

}
