// renderer.cpp — dual-core band pipeline (carried over from Cave Escape, where it
// held 25.3 fps locked). Core 1 composes into ping-pong SRAM bands; core 0 pushes.
// Frame time ~= max(compose, push), not the sum.
#include "renderer.h"
#include "display.h"
#include "config.h"
#include "board_config.h"
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

namespace {

constexpr int NUM_BUFS = 2;
lgfx::LGFX_Sprite s_bands[NUM_BUFS];   // ping-pong compose buffers (internal SRAM)

struct BandMsg { uint8_t idx; int16_t y; };
QueueHandle_t s_qFree  = nullptr;
QueueHandle_t s_qReady = nullptr;

renderer::ComposeFn s_layers[RENDER_MAX_LAYERS];
bool s_swap = false;                   // band buffer stores byte-swapped RGB565?

// Fill a 1x1 sprite red through the library (guaranteed-correct path), read the raw
// word back: tells us how direct buffer writes must be encoded.
void byteOrderSelfTest() {
  lgfx::LGFX_Sprite t;
  t.setColorDepth(16);
  t.setPsram(false);
  if (t.createSprite(1, 1) == nullptr) { Serial.println("[renderer] self-test alloc FAILED"); return; }
  const uint16_t red = 0xF800;
  t.fillRect(0, 0, 1, 1, red);
  const uint16_t v = ((uint16_t*)t.getBuffer())[0];
  s_swap = (v != red);
  Serial.printf("[renderer] band buffer byte order: %s (red reads 0x%04X)\n",
                s_swap ? "SWAPPED" : "native", v);
  t.deleteSprite();
}
int s_nLayers = 0;

void pushTask(void*) {
  auto& lcd = display::lcd();
  BandMsg m;
  for (;;) {
    xQueueReceive(s_qReady, &m, portMAX_DELAY);
    s_bands[m.idx].pushSprite(&lcd, 0, m.y);
    uint8_t idx = m.idx;
    xQueueSend(s_qFree, &idx, portMAX_DELAY);
  }
}

} // namespace

namespace renderer {

bool init() {
  byteOrderSelfTest();
  for (auto& b : s_bands) {
    b.setColorDepth(16);
    b.setPsram(false);  // MUST be internal SRAM — PSRAM bandwidth is shared with panel DMA
    if (b.createSprite(LCD_WIDTH, BAND_HEIGHT) == nullptr) return false;
  }
  s_qFree  = xQueueCreate(NUM_BUFS, sizeof(uint8_t));
  s_qReady = xQueueCreate(NUM_BUFS, sizeof(BandMsg));
  if (!s_qFree || !s_qReady) return false;
  for (uint8_t i = 0; i < NUM_BUFS; ++i) xQueueSend(s_qFree, &i, 0);

  // Push task can saturate core 0; without this the idle-task WDT resets the chip.
  disableCore0WDT();
  return xTaskCreatePinnedToCore(pushTask, "bandpush", 4096, nullptr, 3, nullptr, 0) == pdPASS;
}

bool addLayer(ComposeFn fn) {
  if (!fn || s_nLayers >= RENDER_MAX_LAYERS) return false;
  s_layers[s_nLayers++] = fn;
  return true;
}

void clearLayers() { s_nLayers = 0; }

uint16_t raw(uint16_t c) { return s_swap ? (uint16_t)((c >> 8) | (c << 8)) : c; }

void renderFrame() {
  for (int32_t bandY = 0; bandY < LCD_HEIGHT; bandY += BAND_HEIGHT) {
    uint8_t idx;
    xQueueReceive(s_qFree, &idx, portMAX_DELAY);
    for (int i = 0; i < s_nLayers; ++i) s_layers[i](s_bands[idx], bandY);
    BandMsg m{idx, (int16_t)bandY};
    xQueueSend(s_qReady, &m, portMAX_DELAY);
  }
}

} // namespace renderer
