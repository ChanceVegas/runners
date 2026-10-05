// main.cpp — R0: toolchain proof for Runners. Fixed-timestep loop + band
// renderer + touch, with a test-pattern layer so the pipeline is visibly alive.
// No gameplay. R1 replaces the test layer with the pseudo-3D lane renderer.
#include <Arduino.h>
#include "display.h"
#include "renderer.h"
#include "input.h"
#include "config.h"
#include "board_config.h"

static const float    UPDATE_DT = 1.0f / UPDATE_HZ;

// Panel refresh period, from the RGB timings: 535 x 296 clocks @ 4 MHz = 39,590 us
// (25.26 Hz). The panel can't show frames faster than this, so render is paced to
// it: rendering faster wastes CPU and writes the framebuffer mid-scan (tearing,
// render-vs-refresh beat). Carried over from Cave Escape; omitted in R0-R2 by
// mistake, which ran unpaced at 43 fps (R0-3).
static const uint32_t FRAME_US =
    (uint64_t)(LCD_WIDTH  + LCD_HSYNC_FRONT_PORCH + LCD_HSYNC_PULSE_WIDTH + LCD_HSYNC_BACK_PORCH) *
    (LCD_HEIGHT + LCD_VSYNC_FRONT_PORCH + LCD_VSYNC_PULSE_WIDTH + LCD_VSYNC_BACK_PORCH) *
    1000000ULL / LCD_PCLK_HZ;
static float          s_scroll = 0.0f;   // test-pattern phase, px

// R0 test layer: vertical bars scrolling right at 120 px/s (proves continuous
// motion + invisible band seams) and a marker at the smoothed touch point.
// Pattern period = 64 px (light bar + dark bar, 32 px each). The scroll offset
// MUST wrap at the full period: wrapping at 32 swapped the two shades every
// cycle and looked like the bars jerking backward (R0 bug, fixed in R0-R2).
static void testLayer(lgfx::LGFX_Sprite& band, int32_t bandY) {
  const int32_t off = ((int32_t)s_scroll) % 64;
  band.fillScreen(0x18E3);                              // dark bars = background
  for (int32_t x = off - 64; x < LCD_WIDTH; x += 64)
    band.fillRect(x, 0, 32, band.height(), 0x39C7);     // light bars
  const input::State& in = input::state();
  if (in.touching && in.pointX >= 0) {
    int32_t ly = in.pointY - bandY;
    if (ly > -10 && ly < band.height() + 10)
      band.fillCircle(in.pointX, ly, 8, COLOR_TOUCH_DEBUG);
  }
}

static uint32_t s_frames = 0, s_statT0 = 0;
static uint32_t s_msMin = 0xFFFFFFFF, s_msMax = 0, s_msSum = 0;

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== Runners R0-R3 ===");
  if (!display::init())  { Serial.println("FATAL: display init failed");  for(;;) delay(1000); }
  if (!renderer::init()) { Serial.println("FATAL: renderer init failed"); for(;;) delay(1000); }
  if (!input::init())    { Serial.println("FATAL: input init failed");    for(;;) delay(1000); }
  renderer::addLayer(testLayer);
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
    s_scroll += 120.0f * UPDATE_DT;
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
  renderer::renderFrame();
  uint32_t ms = millis() - t0;
  ++s_frames; s_msSum += ms;
  if (ms < s_msMin) s_msMin = ms;
  if (ms > s_msMax) s_msMax = ms;

  uint32_t nowMs = millis();
  if (nowMs - s_statT0 >= 1000) {
    const input::State& in = input::state();
    Serial.printf("fps: %.1f | render ms avg %.1f min %lu max %lu | heap %u | moveX %.2f touch %d xy %d,%d\n",
                  s_frames * 1000.0f / (nowMs - s_statT0),
                  s_frames ? (float)s_msSum / s_frames : 0.0f,
                  (unsigned long)s_msMin, (unsigned long)s_msMax,
                  (unsigned)ESP.getFreeHeap(), in.moveX, in.touching ? 1 : 0,
                  in.pointX, in.pointY);
    s_frames = 0; s_msSum = 0; s_msMin = 0xFFFFFFFF; s_msMax = 0;
    s_statT0 = nowMs;
  }
}
