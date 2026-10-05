// main.cpp — R1: the encounter mode as a standalone playable loop. Fixed-timestep
// logic (60 Hz) + render paced to the panel refresh, with interpolation between
// ticks. Layers, back to front: road (lanes) -> obstacles + runner -> HUD.
#include <Arduino.h>
#include "display.h"
#include "renderer.h"
#include "input.h"
#include "lanes.h"
#include "encounter.h"
#include "config.h"
#include "board_config.h"

static const float UPDATE_DT = 1.0f / UPDATE_HZ;

// Panel refresh period, from the RGB timings: 535 x 296 clocks @ 4 MHz = 39,590 us
// (25.26 Hz). The panel can't show frames faster than this, so render is paced to
// it: rendering faster wastes CPU and writes the framebuffer mid-scan (tearing,
// render-vs-refresh beat). Carried over from Cave Escape (R0-3).
static const uint32_t FRAME_US =
    (uint64_t)(LCD_WIDTH  + LCD_HSYNC_FRONT_PORCH + LCD_HSYNC_PULSE_WIDTH + LCD_HSYNC_BACK_PORCH) *
    (LCD_HEIGHT + LCD_VSYNC_FRONT_PORCH + LCD_VSYNC_PULSE_WIDTH + LCD_VSYNC_BACK_PORCH) *
    1000000ULL / LCD_PCLK_HZ;

static uint32_t s_frames = 0, s_statT0 = 0;
static uint32_t s_msMin = 0xFFFFFFFF, s_msMax = 0, s_msSum = 0;

static const char* stateName(encounter::State s) {
  switch (s) {
    case encounter::State::Ready: return "READY";
    case encounter::State::Run:   return "RUN";
    case encounter::State::Win:   return "WIN";
    case encounter::State::Lose:  return "LOSE";
  }
  return "?";
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== Runners R1-R2 ===");
  if (!display::init())   { Serial.println("FATAL: display init failed");   for(;;) delay(1000); }
  if (!renderer::init())  { Serial.println("FATAL: renderer init failed");  for(;;) delay(1000); }
  if (!input::init())     { Serial.println("FATAL: input init failed");     for(;;) delay(1000); }
  if (!lanes::init())     { Serial.println("FATAL: lanes init failed");     for(;;) delay(1000); }
  if (!encounter::init()) { Serial.println("FATAL: encounter init failed"); for(;;) delay(1000); }
  renderer::addLayer(lanes::composeBand);
  renderer::addLayer(encounter::composeObjects);
  renderer::addLayer(encounter::composeHud);
  Serial.printf("post-init heap free: %u | PSRAM free: %u\n",
                (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram());
  s_statT0 = millis();
}

void loop() {
  static uint32_t last = micros();
  static float acc = 0.0f;
  uint32_t now = micros();
  float dt = (now - last) * 1e-6f;
  last = now;
  if (dt > 0.25f) dt = 0.25f;           // clamp after stalls (no spiral of death)
  acc += dt;

  while (acc >= UPDATE_DT) {
    input::update(UPDATE_DT);
    encounter::update(UPDATE_DT);       // consumes input::state() edges this tick
    acc -= UPDATE_DT;
  }

  // Frame pacer: lock render cadence to the panel refresh period.
  static uint32_t s_nextFrameUs = micros();
  int32_t wait = (int32_t)(s_nextFrameUs - micros());
  if (wait > 2000) delayMicroseconds(wait - 1000);       // coarse sleep
  while ((int32_t)(s_nextFrameUs - micros()) > 0) {}     // fine spin
  s_nextFrameUs += FRAME_US;
  if ((int32_t)(micros() - s_nextFrameUs) > (int32_t)FRAME_US)
    s_nextFrameUs = micros() + FRAME_US;                 // resync after a stall

  uint32_t t0 = millis();
  encounter::beginRender(acc / UPDATE_DT);
  renderer::renderFrame();
  uint32_t ms = millis() - t0;
  ++s_frames; s_msSum += ms;
  if (ms < s_msMin) s_msMin = ms;
  if (ms > s_msMax) s_msMax = ms;

  uint32_t nowMs = millis();
  if (nowMs - s_statT0 >= 1000) {
    Serial.printf("fps: %.1f | render ms avg %.1f min %lu max %lu | heap %u | %s dist %d m speed %d km/h\n",
                  s_frames * 1000.0f / (nowMs - s_statT0),
                  s_frames ? (float)s_msSum / s_frames : 0.0f,
                  (unsigned long)s_msMin, (unsigned long)s_msMax,
                  (unsigned)ESP.getFreeHeap(), stateName(encounter::state()),
                  (int)encounter::distanceM(), (int)(encounter::speedMS() * 3.6f));
    s_frames = 0; s_msSum = 0; s_msMin = 0xFFFFFFFF; s_msMax = 0;
    s_statT0 = nowMs;
  }
}
