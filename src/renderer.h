// renderer.h — band compositor: compose 34-px bands in internal SRAM (core 1),
// bulk-push them to the PSRAM framebuffer on core 0. Scene-agnostic: callers
// register compose callbacks, drawn back-to-front (painter's order) per band.
#pragma once
#include <LovyanGFX.hpp>
#include <stdint.h>

namespace renderer {

// Draw your layer into `band`, which covers screen rows [bandY, bandY+band.height()).
// Band-local y = screenY - bandY. Must not allocate.
using ComposeFn = void (*)(lgfx::LGFX_Sprite& band, int32_t bandY);

bool init();                       // allocate band buffers (SRAM) + start push task
bool addLayer(ComposeFn fn);       // register a layer; earlier = further back
void clearLayers();                // drop all layers (mode switches: overworld <-> encounter)
void renderFrame();                // compose + push every band through all layers

}
