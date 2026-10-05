// input.cpp — single-touch drag joystick on XPT2046 (via display's LGFX touch).
// Touch-down anchors; horizontal drag = moveX (EMA-smoothed, dead-zoned).
// Jump = RAW-y displacement above anchor OR per-sample upward velocity (fast
// snaps get anchored mid-flick at ~25 Hz effective sampling), plus a
// release-edge displacement check using the second-to-last sample (the final
// resistive sample at liftoff is often garbage). Verified on hardware 2026-07-13.
// NOTE: jumpPressed is a ONE-TICK edge — consumers must read it per logic tick.
#include "input.h"
#include "display.h"
#include "config.h"
#include "board_config.h"
#include <Arduino.h>

namespace {

input::State s_state = {0.0f, false, false};

bool  s_wasTouching = false;
float s_anchorX = 0, s_anchorY = 0;
float s_emaX = 0, s_emaY = 0;
float s_rawY = 0, s_prevRawY = 0;
bool  s_jumpArmed = true;

inline float clamp1(float v) { return v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v); }

} // namespace

namespace input {

bool init() {
  pinMode(SD_PIN_CS, OUTPUT);      // park shared-bus SD chip select (TF slot unused)
  digitalWrite(SD_PIN_CS, HIGH);
  return true;
}

void update(float dt) {
  (void)dt;
  s_state.jumpPressed = false;

  int32_t rx, ry;
  bool touching = display::lcd().getTouch(&rx, &ry);
  s_state.touching = touching;

  if (touching && !s_wasTouching) {
    s_anchorX = s_emaX = (float)rx;
    s_anchorY = s_emaY = (float)ry;
    s_rawY = s_prevRawY = (float)ry;
    s_jumpArmed = true;
    s_state.moveX = 0.0f;
  } else if (touching) {
    s_emaX += INPUT_EMA_ALPHA * ((float)rx - s_emaX);
    s_emaY += INPUT_EMA_ALPHA * ((float)ry - s_emaY);
    s_prevRawY = s_rawY;
    s_rawY = (float)ry;

    float dx = s_emaX - s_anchorX;
    if (dx >  INPUT_DEADZONE_PX) dx -= INPUT_DEADZONE_PX;
    else if (dx < -INPUT_DEADZONE_PX) dx += INPUT_DEADZONE_PX;
    else dx = 0.0f;
    s_state.moveX = clamp1(dx / (float)INPUT_JOY_RANGE_PX);

    float rise = s_anchorY - s_rawY;
    float step = s_prevRawY - s_rawY;
    if (s_jumpArmed && (rise > INPUT_JUMP_FLICK_PX || step > INPUT_JUMP_VEL_PX)) {
      s_state.jumpPressed = true;
      s_jumpArmed = false;
    } else if (!s_jumpArmed && rise < INPUT_JUMP_REARM_PX) {
      s_jumpArmed = true;
    }
  } else {
    if (s_wasTouching) {
      float rise = s_anchorY - s_prevRawY;
      if (s_jumpArmed && rise > INPUT_JUMP_FLICK_PX) s_state.jumpPressed = true;
    }
    s_state.moveX = 0.0f;
  }
  s_wasTouching = touching;
}

const State& state() { return s_state; }

} // namespace input