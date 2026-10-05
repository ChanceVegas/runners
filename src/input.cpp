// input.cpp — touch gestures on the XPT2046 (via display's LGFX touch), sampled by
// a dedicated task at INPUT_SAMPLE_HZ instead of from the game loop.
//
// Why a task (R1-R2): the game loop runs its 2–3 logic ticks in a burst right
// before each render, so reading touch there sampled the panel only ~25 times a
// second (all ticks in a burst saw the same reading). Flicks and swipes felt
// sluggish and were missed. The sampler task runs the gesture state machine at
// 100 Hz and counts edges; input::update() hands each edge to exactly one game tick.
//
// Gestures: touch-down = tap (pressed) + anchor. Horizontal drag of INPUT_SWIPE_PX
// = one lane swipe (anchor re-centres, so a long drag can swipe twice). Jump =
// rise above the anchor OR fast upward motion over the last INPUT_JUMP_WINDOW
// samples, plus a release-edge check on the second-to-last sample (the final
// resistive sample at liftoff is often garbage).
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
float t_anchorX = 0, t_anchorY = 0;
float t_emaX = 0, t_emaY = 0;
float t_swipeAnchorX = 0;
bool  t_jumpArmed = true;
float t_yHist[INPUT_JUMP_WINDOW + 1];   // recent raw y, newest at [0]
int   t_nHist = 0;

// ---- Shared: written by the sampler under s_mux, read by input::update ----
portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
uint32_t sh_jumps = 0, sh_presses = 0, sh_swipeL = 0, sh_swipeR = 0;
float    sh_moveX = 0;
bool     sh_touching = false;
int16_t  sh_px = -1, sh_py = -1;

// ---- Game-loop side: edges already delivered ----
uint32_t g_jumps = 0, g_presses = 0, g_swipeL = 0, g_swipeR = 0;
input::State s_state = {0.0f, false, 0, false, false, -1, -1};

inline float clamp1(float v) { return v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v); }

void pushHist(float y) {
  for (int i = INPUT_JUMP_WINDOW; i > 0; --i) t_yHist[i] = t_yHist[i - 1];
  t_yHist[0] = y;
  if (t_nHist < INPUT_JUMP_WINDOW + 1) ++t_nHist;
}

void sampleOnce() {
  int32_t rx, ry;
  const bool touching = display::lcd().getTouch(&rx, &ry);
  uint32_t jumps = 0, presses = 0, swL = 0, swR = 0;
  float moveX = 0.0f;

  if (touching && !t_wasTouching) {                  // touch-down
    t_anchorX = t_emaX = t_swipeAnchorX = (float)rx;
    t_anchorY = t_emaY = (float)ry;
    t_nHist = 0;
    pushHist((float)ry);
    t_jumpArmed = true;
    presses = 1;
  } else if (touching) {                             // held
    t_emaX += INPUT_EMA_ALPHA * ((float)rx - t_emaX);
    t_emaY += INPUT_EMA_ALPHA * ((float)ry - t_emaY);
    pushHist((float)ry);

    float dx = t_emaX - t_anchorX;
    if (dx >  INPUT_DEADZONE_PX) dx -= INPUT_DEADZONE_PX;
    else if (dx < -INPUT_DEADZONE_PX) dx += INPUT_DEADZONE_PX;
    else dx = 0.0f;
    moveX = clamp1(dx / (float)INPUT_JOY_RANGE_PX);

    const float sw = t_emaX - t_swipeAnchorX;
    if (sw > INPUT_SWIPE_PX)       { swR = 1; t_swipeAnchorX = t_emaX; }
    else if (sw < -INPUT_SWIPE_PX) { swL = 1; t_swipeAnchorX = t_emaX; }

    const float rise = t_anchorY - (float)ry;
    const float fast = (t_nHist > INPUT_JUMP_WINDOW)
                     ? t_yHist[INPUT_JUMP_WINDOW] - t_yHist[0] : 0.0f;
    if (t_jumpArmed && (rise > INPUT_JUMP_FLICK_PX || fast > INPUT_JUMP_VEL_PX)) {
      jumps = 1;
      t_jumpArmed = false;
    } else if (!t_jumpArmed && rise < INPUT_JUMP_REARM_PX) {
      t_jumpArmed = true;
    }
  } else if (t_wasTouching) {                        // liftoff
    if (t_jumpArmed && t_nHist >= 2 && (t_anchorY - t_yHist[1]) > INPUT_JUMP_FLICK_PX)
      jumps = 1;
  }
  t_wasTouching = touching;

  portENTER_CRITICAL(&s_mux);
  sh_jumps += jumps; sh_presses += presses; sh_swipeL += swL; sh_swipeR += swR;
  sh_moveX = moveX;
  sh_touching = touching;
  sh_px = touching ? (int16_t)t_emaX : -1;
  sh_py = touching ? (int16_t)t_emaY : -1;
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
  const uint32_t jumps = sh_jumps, presses = sh_presses, swL = sh_swipeL, swR = sh_swipeR;
  s_state.moveX = sh_moveX;
  s_state.touching = sh_touching;
  s_state.pointX = sh_px;
  s_state.pointY = sh_py;
  portEXIT_CRITICAL(&s_mux);

  // Taps and jumps collapse (two between ticks = one action). Swipes are queued:
  // one per tick, so a fast double swipe still moves two lanes.
  s_state.jumpPressed = (jumps != g_jumps);  g_jumps = jumps;
  s_state.pressed     = (presses != g_presses); g_presses = presses;
  s_state.laneSwipe = 0;
  if (swL != g_swipeL)      { s_state.laneSwipe = -1; ++g_swipeL; }
  else if (swR != g_swipeR) { s_state.laneSwipe = +1; ++g_swipeR; }
}

const State& state() { return s_state; }

} // namespace input
