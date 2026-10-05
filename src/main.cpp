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
static float          s_scroll = 0.0f;   // test-pattern phase, px
static int32_t        s_touchX = -1, s_touchY = -1;

// R0 test layer: diagonal stripes scrolling at 120 px/s (proves continuous
// motion + band seams are invisible) and a marker where the finger is.
static void testLayer(lgfx::LGFX_Sprite& band, int32_t bandY) {
  const int32_t off = (int32_t)s_scroll;
  for (int32_t x = -BAND_HEIGHT; x < LCD_WIDTH; x += 32) {
    int32_t sx = x + (off % 32);
    uint16_t c = ((x / 32) & 1) ? 0x2945 : 0x18E3;
    band.fillRect(sx, 0, 16, band.height(), c);
  }
  if (s_touchX >= 0) {
    int32_t ly = s_touchY - bandY;
    if (ly > -10 && ly < band.height() + 10)
      band.fillCircle(s_touchX, ly, 8, COLOR_TOUCH_DEBUG);
  }
}

static uint32_t s_frames = 0, s_statT0 = 0;
static uint32_t s_msMin = 0xFFFFFFFF, s_msMax = 0, s_msSum = 0;

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== Runners R0-R1 ===");
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

  // R0 reads raw touch only for the debug marker; game code will use input::state().
  int32_t tx, ty;
  if (display::lcd().getTouch(&tx, &ty)) { s_touchX = tx; s_touchY = ty; }
  else { s_touchX = s_touchY = -1; }

  uint32_t t0 = millis();
  renderer::renderFrame();
  uint32_t ms = millis() - t0;
  ++s_frames; s_msSum += ms;
  if (ms < s_msMin) s_msMin = ms;
  if (ms > s_msMax) s_msMax = ms;

  uint32_t nowMs = millis();
  if (nowMs - s_statT0 >= 1000) {
    const input::State& in = input::state();
    Serial.printf("fps: %.1f | render ms avg %.1f min %lu max %lu | heap %u | moveX %.2f touch %d\n",
                  s_frames * 1000.0f / (nowMs - s_statT0),
                  s_frames ? (float)s_msSum / s_frames : 0.0f,
                  (unsigned long)s_msMin, (unsigned long)s_msMax,
                  (unsigned)ESP.getFreeHeap(), in.moveX, in.touching ? 1 : 0);
    s_frames = 0; s_msSum = 0; s_msMin = 0xFFFFFFFF; s_msMax = 0;
    s_statT0 = nowMs;
  }
}
