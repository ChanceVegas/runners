// input.h — touch gestures → abstract game actions (backend-swappable; PLANNING #5).
// Game code sees ONLY moveX/jump — never touch coordinates — so a BLE gamepad
// backend can replace poll internals without touching game code.
#pragma once
#include <stdint.h>

namespace input {

struct State {
  float moveX;        // -1..1, dead-zoned, smoothed
  bool  jumpPressed;  // edge: true for exactly one update tick per flick
  bool  touching;     // finger down (debug/HUD use only, not gameplay)
  int16_t pointX;     // smoothed screen-space touch point (debug/UI only, not
  int16_t pointY;     //   gameplay); valid while touching. Single touch read/tick.
};

bool init();                 // driver init + SD-CS guard; call after display::init
void update(float dt);       // poll + gesture state machine; call once per logic tick
const State& state();        // current abstract actions
}
