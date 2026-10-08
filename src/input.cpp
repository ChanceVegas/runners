// input.cpp — touch on the XPT2046 (via display's LGFX touch), sampled by a
// dedicated task at INPUT_SAMPLE_HZ instead of from the game loop.
//
// Controls = TAP ZONES (R1-R3, user decision 2026-10-06): the screen is split into
// thirds; a tap in the left/right third steps one lane, a tap in the middle jumps
// (upper part) or ducks (bottom part, y >= INPUT_ZONE_DUCK_Y; B1-R3).
// Zones replaced swipe/flick gestures, which had to watch several samples before
// recognising anything, so they always lagged on resistive touch. A zone tap fires
// on the second sample after touch-down (10 ms): the first resistive sample at
// touch-down is often off, so we wait one sample and discard one-sample bounces.
//
// Why a task (R1-R2): the game loop runs its logic ticks in a burst right before
// each render, so reading touch there sampled the panel only ~25 times a second.
// The task samples at 100 Hz and counts taps; input::update() hands each tap to
// exactly one game tick.
#include "input.h"
#include "display.h"
#include "config.h"
#include "board_config.h"
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {

// ---- Sampler-task-only state (never touched by the game loop) ----
bool  t_wasTouching = false;
int   t_heldSamples = 0;                // samples since touch-down
float t_anchorX = 0, t_anchorY = 0;
float t_emaX = 0, t_emaY = 0;

// ---- Shared: written by the sampler under s_mux, read by input::update ----
portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
uint32_t sh_jumps = 0, sh_ducks = 0, sh_presses = 0, sh_stepL = 0, sh_stepR = 0;
float    sh_moveX = 0, sh_moveY = 0;
bool     sh_touching = false;
int16_t  sh_px = -1, sh_py = -1;
int16_t  sh_tapX = -1, sh_tapY = -1;     // last confirmed tap position

// ---- Game-loop side: edges already delivered ----
uint32_t g_jumps = 0, g_ducks = 0, g_presses = 0, g_stepL = 0, g_stepR = 0;
input::State s_state = {0.0f, 0.0f, false, false, 0, false, false, -1, -1, -1, -1};

inline float clamp1(float v) { return v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v); }

void sampleOnce() {
  int32_t rx, ry;
  const bool touching = display::lcd().getTouch(&rx, &ry);
  uint32_t jumps = 0, ducks = 0, presses = 0, stepL = 0, stepR = 0;
  float moveX = 0.0f, moveY = 0.0f;

  if (touching && !t_wasTouching) {                  // touch-down: wait one sample
    t_heldSamples = 1;
    t_emaX = (float)rx;
    t_emaY = (float)ry;
  } else if (touching) {
    ++t_heldSamples;
    if (t_heldSamples == 2) {                        // confirmed tap: classify by zone
      t_anchorX = t_emaX = (float)rx;                // 2nd sample, not the touch-down one
      t_anchorY = t_emaY = (float)ry;
      presses = 1;
      if (rx < INPUT_ZONE_LEFT_X)       stepL = 1;
      else if (rx >= INPUT_ZONE_RIGHT_X) stepR = 1;
      else if (ry >= INPUT_ZONE_DUCK_Y)  ducks = 1;
      else                               jumps = 1;
    } else {
      t_emaX += INPUT_EMA_ALPHA * ((float)rx - t_emaX);
      t_emaY += INPUT_EMA_ALPHA * ((float)ry - t_emaY);
    }
    // Drag axes from where the finger landed (overworld walking; not encounter).
    float dx = t_emaX - t_anchorX, dy = t_emaY - t_anchorY;
    if (dx >  INPUT_DEADZONE_PX) dx -= INPUT_DEADZONE_PX;
    else if (dx < -INPUT_DEADZONE_PX) dx += INPUT_DEADZONE_PX;
    else dx = 0.0f;
    if (dy >  INPUT_DEADZONE_PX) dy -= INPUT_DEADZONE_PX;
    else if (dy < -INPUT_DEADZONE_PX) dy += INPUT_DEADZONE_PX;
    else dy = 0.0f;
    moveX = clamp1(dx / (float)INPUT_JOY_RANGE_PX);
    moveY = clamp1(dy / (float)INPUT_JOY_RANGE_PX);
  }
  t_wasTouching = touching;

  portENTER_CRITICAL(&s_mux);
  sh_jumps += jumps; sh_ducks += ducks; sh_presses += presses; sh_stepL += stepL; sh_stepR += stepR;
  if (presses) { sh_tapX = (int16_t)rx; sh_tapY = (int16_t)ry; }
  sh_moveX = moveX;
  sh_moveY = moveY;
  sh_touching = touching && t_heldSamples >= 2;
  sh_px = sh_touching ? (int16_t)t_emaX : -1;
  sh_py = sh_touching ? (int16_t)t_emaY : -1;
  portEXIT_CRITICAL(&s_mux);
}

void samplerTask(void*) {
  TickType_t last = xTaskGetTickCount();
  const TickType_t period = pdMS_TO_TICKS(1000 / INPUT_SAMPLE_HZ);
  for (;;) {
    sampleOnce();
    vTaskDelayUntil(&last, period);
  }
}

} // namespace

namespace input {

bool init() {
  pinMode(SD_PIN_CS, OUTPUT);      // park shared-bus SD chip select (TF slot unused)
  digitalWrite(SD_PIN_CS, HIGH);
  // Core 1 alongside the game loop (priority 1); higher priority so samples stay
  // on schedule while compose runs. Each sample is a short SPI read.
  return xTaskCreatePinnedToCore(samplerTask, "touch", 4096, nullptr, 2, nullptr, 1) == pdPASS;
}

void update(float dt) {
  (void)dt;
  portENTER_CRITICAL(&s_mux);
  const uint32_t jumps = sh_jumps, ducks = sh_ducks, presses = sh_presses, stL = sh_stepL, stR = sh_stepR;
  s_state.moveX = sh_moveX;
  s_state.moveY = sh_moveY;
  s_state.touching = sh_touching;
  s_state.pointX = sh_px;
  s_state.pointY = sh_py;
  s_state.tapX = sh_tapX;
  s_state.tapY = sh_tapY;
  portEXIT_CRITICAL(&s_mux);

  // Jumps, ducks and presses collapse (two between ticks = one action). Lane steps are
  // queued, one per tick, so two quick taps on one side move two lanes.
  s_state.jumpPressed = (jumps != g_jumps);  g_jumps = jumps;
  s_state.duckPressed = (ducks != g_ducks);  g_ducks = ducks;
  s_state.pressed     = (presses != g_presses); g_presses = presses;
  s_state.laneStep = 0;
  if (stL != g_stepL)      { s_state.laneStep = -1; ++g_stepL; }
  else if (stR != g_stepR) { s_state.laneStep = +1; ++g_stepR; }
}

const State& state() { return s_state; }

} // namespace input
