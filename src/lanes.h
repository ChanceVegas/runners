// lanes.h — pseudo-3D road renderer for encounters: sky, hills, 3-lane road with
// scrolling stripes and curves, plus the world->screen projection everything else
// on the road uses. Draws by writing the SRAM band buffer directly (no per-pixel calls).
#pragma once
#include <LovyanGFX.hpp>
#include <stdint.h>

namespace lanes {

// Allocates nothing. Runs a byte-order self-test on a 1x1 scratch sprite (direct
// buffer writes must match how LovyanGFX stores 16-bit pixels) and prints it.
bool init();

// Latch this frame's road state. travel = metres run (interpolated). Computes the
// bend and the per-row depth/width/stripe tables used by composeBand and project.
void beginFrame(float travel);

// Draw sky, hills and road into the band covering rows [bandY, bandY+h).
void composeBand(lgfx::LGFX_Sprite& band, int32_t bandY);

// Project a point on the road at world depth z (m ahead of the camera) and lane
// position (-1 left, 0 centre, +1 right; fractions allowed) to screen space.
// Out: sx = centre x, sy = ground y, s = size scale (1 at the bottom row).
// Returns false if the point is beyond the horizon or behind the camera.
bool project(float z, float lane, float& sx, float& sy, float& s);

// World depth (m) of the road at screen row y; y must be below the horizon.
float depthAtRow(int32_t y);

}
