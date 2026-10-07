// world.cpp — terrain = 2-octave value noise (elevation) + moisture + a "trail" noise
// band, sampled on INTEGER tile indices with integer noise periods, so results are
// exact for any absolute coordinate. Spawns: hash(cellX, cellY, window) decides
// presence, kind and tile inside the cell; water tiles never hold an enemy.
#include "world.h"
#include "config.h"
#include <Arduino.h>
#include <math.h>

namespace {

inline uint32_t mix(uint32_t h) {
  h ^= h >> 16; h *= 0x7feb352dU; h ^= h >> 15; h *= 0x846ca68bU; h ^= h >> 16;
  return h;
}
inline uint32_t hash3(int32_t a, int32_t b, uint32_t c) {
  return mix((uint32_t)a * 0x9E3779B1U ^ mix((uint32_t)b * 0x85EBCA77U ^ mix(c * 0xC2B2AE3DU + 0x27D4EB2FU)));
}
inline int32_t floorDiv(int32_t a, int32_t b) {
  int32_t q = a / b;
  if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
  return q;
}
inline float lattice(int32_t x, int32_t y, uint32_t seed) {
  return (hash3(x, y, seed) & 0xFFFF) / 65535.0f;
}
inline float smooth(float t) { return t * t * (3.0f - 2.0f * t); }

// Value noise at integer tile coords with an integer period: exact for any |tx|.
float noise(int32_t tx, int32_t ty, int32_t period, uint32_t seed) {
  const int32_t cx = floorDiv(tx, period), cy = floorDiv(ty, period);
  const float fx = smooth(((tx - cx * period) + 0.5f) / period);
  const float fy = smooth(((ty - cy * period) + 0.5f) / period);
  const float a = lattice(cx, cy, seed),     b = lattice(cx + 1, cy, seed);
  const float c = lattice(cx, cy + 1, seed), d = lattice(cx + 1, cy + 1, seed);
  const float top = a + (b - a) * fx, bot = c + (d - c) * fx;
  return top + (bot - top) * fy;
}

struct Memo { bool used; bool escaped; bool revealed; int32_t cx, cy; uint32_t window; };
Memo s_memo[OW_DEFEATED_SLOTS];
int  s_memoNext = 0;

Memo* findMemo(int32_t cx, int32_t cy, uint32_t w) {
  for (auto& m : s_memo) if (m.used && m.cx == cx && m.cy == cy && m.window == w) return &m;
  return nullptr;
}
Memo& memo(int32_t cx, int32_t cy, uint32_t w) {
  if (Memo* m = findMemo(cx, cy, w)) return *m;
  Memo& m = s_memo[s_memoNext];
  s_memoNext = (s_memoNext + 1) % OW_DEFEATED_SLOTS;
  m = { true, false, false, cx, cy, w };
  return m;
}

} // namespace

namespace world {

Tile terrainAt(int32_t tx, int32_t ty) {
  const int32_t P = OW_NOISE_SCALE_T;
  const float elev  = 0.65f * noise(tx, ty, P, 11) + 0.35f * noise(tx, ty, P / 3 + 1, 23);
  const float moist = noise(tx + 7919, ty - 104729, P + P / 2, 37);
  const float trail = noise(tx - 31337, ty + 4242, P, 53);
  const uint8_t deco = (uint8_t)(hash3(tx, ty, 99) & 0xFF);

  Terrain t;
  if (elev < OW_WATER_LEVEL)        t = Terrain::Water;
  else if (elev < OW_SAND_LEVEL)    t = Terrain::Sand;
  else if (elev > OW_ROCK_LEVEL)    t = Terrain::Rock;
  else if (fabsf(trail - 0.5f) < OW_TRAIL_WIDTH) t = Terrain::Trail;
  else if (moist > OW_FOREST_MOIST) t = Terrain::Forest;
  else                              t = Terrain::Grass;
  return { t, deco };
}

uint32_t window() { return (uint32_t)(millis() / 1000) / OW_SPAWN_WINDOW_S; }

uint32_t secondsLeftInWindow() {
  const uint32_t s = (uint32_t)(millis() / 1000);
  return OW_SPAWN_WINDOW_S - (s % OW_SPAWN_WINDOW_S);
}

int enemiesNear(const locator::Pos& c, int radiusTiles, bool running, Enemy* out, int maxOut) {
  const uint32_t w = window();
  const int32_t C = OW_SPAWN_CELL_T;
  const int32_t cx0 = floorDiv(c.tx - radiusTiles, C), cx1 = floorDiv(c.tx + radiusTiles, C);
  const int32_t cy0 = floorDiv(c.ty - radiusTiles, C), cy1 = floorDiv(c.ty + radiusTiles, C);
  int n = 0;
  for (int32_t cy = cy0; cy <= cy1; ++cy) {
    for (int32_t cx = cx0; cx <= cx1; ++cx) {
      if (n >= maxOut) return n;
      const uint32_t h = hash3(cx, cy, w * 2654435761U + 17);
      if ((int)(h % 100) >= OW_SPAWN_CHANCE) continue;
      const int32_t tx = cx * C + (int32_t)((h >> 12) % C);
      const int32_t ty = cy * C + (int32_t)((h >> 20) % C);
      if (terrainAt(tx, ty).terrain == Terrain::Water) continue;

      EnemyKind k;
      const bool rare = (int)((h >> 8) % 100) < OW_RARE_CHANCE;
      if (rare) k = EnemyKind::Phantom;
      else k = ((int)((h >> 4) % 100) < 30) ? EnemyKind::Brute : EnemyKind::Shade;

      Memo* m = findMemo(cx, cy, w);
      if (m && m->escaped) continue;
      if (rare) {
        if (running) memo(cx, cy, w).revealed = true;       // running reveals it
        else if (!(m && m->revealed)) continue;            // hidden until revealed
      }
      out[n++] = { cx, cy, w, k, { tx, ty, OW_TILE_M * 0.5f, OW_TILE_M * 0.5f }, rare };
    }
  }
  return n;
}

void markEscaped(const Enemy& e) { memo(e.cx, e.cy, e.window).escaped = true; }

uint8_t level(EnemyKind k) {
  switch (k) { case EnemyKind::Shade: return 1; case EnemyKind::Brute: return 2; default: return 3; }
}
float goalM(EnemyKind k) {
  switch (k) {
    case EnemyKind::Shade: return ENEMY_SHADE_GOAL_M;
    case EnemyKind::Brute: return ENEMY_BRUTE_GOAL_M;
    default:               return ENEMY_PHANTOM_GOAL_M;
  }
}
const char* name(EnemyKind k) {
  switch (k) { case EnemyKind::Shade: return "SHADE"; case EnemyKind::Brute: return "BRUTE"; default: return "PHANTOM"; }
}

} // namespace world
