// scenery.h — roadside props (pines, bushes, marker posts) placed deterministically
// along the road from a hash of their slot index: no pool, no state, no spawning.
// Pure decoration; never collides. Drawn behind obstacles and the runner.
#pragma once
#include <LovyanGFX.hpp>
#include <stdint.h>

namespace scenery {

// Latch this frame's travel (m) and build the far-to-near draw list.
void beginFrame(float travel);

// Draw the props intersecting the band covering rows [bandY, bandY+h).
void composeBand(lgfx::LGFX_Sprite& band, int32_t bandY);

}
