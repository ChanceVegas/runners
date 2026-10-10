// world.h — the deterministic overworld: terrain per tile and enemy spawns per spawn
// cell, both pure functions of grid indices (+ a time window for spawns). Nothing is
// stored per tile, so the world is infinite and identical on every device at the same
// coordinates. Also remembers which enemies the player escaped / revealed this window.
#pragma once
#include <stdint.h>
#include "locator.h"

namespace world {

enum class Terrain : uint8_t { Water, Sand, Grass, Forest, Rock, Trail };

struct Tile {
  Terrain terrain;
  uint8_t deco;        // 0..255 per-tile hash for decoration variety (flowers, trees)
};

enum class EnemyKind : uint8_t { Shade, Brute, Phantom, Stalker, Hornet, Warden };

struct Enemy {
  int32_t cx, cy;      // spawn cell
  uint32_t window;     // spawn window it belongs to
  EnemyKind kind;
  locator::Pos pos;    // world position (centre of its tile)
  bool rare;           // Phantom: only visible while running, until revealed
};

Tile terrainAt(int32_t tx, int32_t ty);

// Current spawn window (index) and seconds until it rolls over. Sim: boot-relative
// clock; R2 replaces it with GPS UTC so all devices share windows.
uint32_t window();
uint32_t secondsLeftInWindow();

// Enemies in spawn cells overlapping a square of +-radiusTiles around centre that are
// not escaped this window. Rare ones are included only if revealed or `running`
// (running also reveals them, so they stay visible after the player stops).
int enemiesNear(const locator::Pos& centre, int radiusTiles, bool running,
                Enemy* out, int maxOut);

void markEscaped(const Enemy& e);

uint8_t level(EnemyKind k);        // 1..3: chase speed tier
uint8_t unlockLevel(EnemyKind k);  // player level needed to fight it (E1)
bool    ready(EnemyKind k);        // battle behaviour exists (E1 kinds go live one by one)
bool    locked(EnemyKind k, uint8_t playerLevel);   // on the map but can't be fought
float   goalM(EnemyKind k);        // chase length, m
const char* name(EnemyKind k);

}
