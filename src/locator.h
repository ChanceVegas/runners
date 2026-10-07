// locator.h — where the player is in the world, how fast they're moving, and whether
// that counts as still / walking / running. Source today: SIMULATED from drag input
// on the overworld (no GPS module yet). R2 swaps in a GPS reader behind this same
// interface; nothing else changes.
#pragma once
#include <stdint.h>

namespace locator {

enum class Move : uint8_t { Still, Walk, Run };

// Position on the absolute world grid: whole tile index + metres within the tile.
// (Avoids float precision loss once real GPS coordinates are in the millions of m.)
struct Pos { int32_t tx, ty; float fx, fy; };

bool init(const Pos& start);       // start position (e.g. restored from flash)
void update(float dt);             // one logic tick (sim: reads input::state())
void setEnabled(bool on);          // sim: ignore input while not on the overworld

Pos   pos();
float speedMS();                   // ground speed, m/s (real-world scale)
Move  move();
float headingRad();                // last direction of travel (0 = east, + = south)

// Offset in metres from a to b (b - a), exact across tile boundaries.
void deltaM(const Pos& a, const Pos& b, float& dx, float& dy);

}
