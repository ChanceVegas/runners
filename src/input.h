// input.h — touch gestures → abstract game actions (backend-swappable; PLANNING #5).
// Game code sees ONLY moveX/jump/duck — never touch coordinates — so a BLE gamepad
// backend can replace poll internals without touching game code.
#pragma once
#include <stdint.h>

namespace input {

struct State {
  float moveX;        // -1..1 horizontal drag from where the finger landed (dead-zoned)
  float moveY;        // -1..1 vertical drag, + = down the screen (dead-zoned)
  bool  jumpPressed;  // edge: one tick per tap in the MIDDLE zone, upper part
  bool  duckPressed;  // edge: one tick per tap in the MIDDLE zone, bottom part
  int8_t laneStep;    // edge: -1 / +1 for one tick per tap in the LEFT / RIGHT zone
  bool  pressed;      // edge: one tick per tap anywhere (menus, start, retry)
  bool  touching;     // finger down (debug/HUD use only, not gameplay)
  int16_t pointX;     // smoothed screen-space touch point (debug/UI only, not
  int16_t pointY;     //   gameplay); valid while touching. Single touch read/tick.
};

bool init();                 // driver init + SD-CS guard; call after display::init
void update(float dt);       // poll + gesture state machine; call once per logic tick
const State& state();        // current abstract actions
}
