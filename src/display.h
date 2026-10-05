// display.h — Panel init + access to the LGFX device (NV3047 RGB, LovyanGFX).
#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>

namespace display {
bool init();                 // bring up panel + backlight; true on success
lgfx::LGFX_Device& lcd();    // the panel device (PSRAM framebuffer behind it)
}
