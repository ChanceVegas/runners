// overworld.cpp — top-down map. Camera keeps the player at (OW_PLAYER_SX, SY). The
// visible tiles' terrain is cached and only recomputed when the view crosses a tile
// boundary. Terrain rows are filled directly into the SRAM band buffer (colours via
// renderer::raw, R1-BYTE), so a full-screen map costs a few ms, not thousands of calls.
#include "overworld.h"
#include "locator.h"
#include "renderer.h"
#include "input.h"
#include "hud.h"
#include "color.h"
#include "config.h"
#include "board_config.h"
#include <Arduino.h>
#include <math.h>
#include <stdio.h>

namespace {

using world::Terrain;

constexpr int32_t PLAYER_SX = LCD_WIDTH / 2;      // avatar screen position
constexpr int32_t PLAYER_SY = 154;
constexpr float   PX_PER_M  = (float)OW_TILE_PX / OW_TILE_M;
constexpr int     COLS = LCD_WIDTH / OW_TILE_PX + 2;    // visible tiles + partial edges
constexpr int     ROWS = LCD_HEIGHT / OW_TILE_PX + 2;
constexpr int     MAX_ENEMIES = 12;
constexpr int32_t TOP_BAR_H = 32;
constexpr int32_t MENU_X = 4, MENU_Y = 3, MENU_W = 78, MENU_H = 26;

// ---- Logic state ----
locator::Pos s_cur = {0, 0, 0, 0}, s_prev = {0, 0, 0, 0};
world::Enemy s_enemies[MAX_ENEMIES];
int   s_nEnemies = 0;
int   s_target = -1;                 // index into s_enemies of the engage candidate
float s_hold = 0.0f;                 // s still while in range
bool  s_engaged = false;
world::Enemy s_engagedEnemy;
bool  s_needMove = false;            // after a chase: must move before engaging again
bool  s_menuReq = false;
float s_energy = 0.0f;
float s_walked = 0.0f;
uint32_t s_wallet = 0, s_escapes = 0;
locator::Move s_move = locator::Move::Still;

// ---- Render state ----
locator::Pos d_pos = {0, 0, 0, 0};
int32_t d_camX = 0, d_camY = 0;      // world pixel at screen (0,0)
int32_t d_firstTx = 0, d_firstTy = 0, d_offX = 0, d_offY = 0;
int32_t c_firstTx = INT32_MIN, c_firstTy = INT32_MIN;   // tile cache key
world::Tile s_tiles[ROWS][COLS];
float d_time = 0.0f;
struct EnemyDraw { int16_t x, y; uint8_t kind; bool target; uint8_t phase; };
EnemyDraw s_edraw[MAX_ENEMIES];
int s_nEdraw = 0;

// Palette in band-buffer order (built at init, after renderer's byte-order test).
uint16_t P_WATER[2], P_WATER_HI, P_SAND[2], P_SAND_DOT, P_GRASS[2], P_TUFT, P_FLOWER[3];
uint16_t P_FOREST_GND, P_CANOPY, P_CANOPY_HI, P_ROCK[2], P_ROCK_DARK, P_TRAIL[2], P_TRAIL_EDGE;
uint8_t s_canopyHalf[OW_TILE_PX];    // tree canopy half-width per tile row (circle)
uint8_t s_canopyHiHalf[OW_TILE_PX];  // smaller highlight circle, offset up-left

inline int32_t floorDiv(int32_t a, int32_t b) {
  int32_t q = a / b;
  if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
  return q;
}

inline void fill(uint16_t* row, int32_t x0, int32_t x1, uint16_t c) {
  if (x0 < 0) x0 = 0;
  if (x1 > LCD_WIDTH) x1 = LCD_WIDTH;
  for (int32_t x = x0; x < x1; ++x) row[x] = c;
}

// One tile's pixels for tile-row iy, starting at screen x0.
inline void tileRow(uint16_t* row, int32_t x0, int32_t iy, const world::Tile& t,
                    int32_t tx, int32_t ty, int32_t wave) {
  const int32_t T = OW_TILE_PX;
  const uint8_t d = t.deco;
  const int alt = (tx + ty) & 1;
  switch (t.terrain) {
    case Terrain::Water: {
      fill(row, x0, x0 + T, P_WATER[alt]);
      if (((iy + wave + (d & 7)) % 12) == 0) {                 // drifting wave glints
        const int32_t gx = x0 + 3 + (d >> 3) % 12;
        fill(row, gx, gx + 7, P_WATER_HI);
      }
      break;
    }
    case Terrain::Sand:
      fill(row, x0, x0 + T, P_SAND[alt]);
      if (iy == 4 + (d & 7) || iy == 14 + ((d >> 3) & 7)) {
        const int32_t sx = x0 + 2 + ((d >> 2) % 18);
        fill(row, sx, sx + 2, P_SAND_DOT);
      }
      break;
    case Terrain::Grass:
      fill(row, x0, x0 + T, P_GRASS[alt]);
      if ((d % 5) == 0 && (iy == 6 + (d & 7) || iy == 7 + (d & 7))) {   // flower
        const int32_t fx = x0 + 4 + ((d >> 4) % 14);
        fill(row, fx, fx + 3, P_FLOWER[d % 3]);
      } else if ((d % 3) == 1 && iy >= 12 && iy <= 14) {             // grass tuft
        const int32_t gx = x0 + 3 + ((d >> 2) % 16);
        fill(row, gx, gx + 1, P_TUFT);
        fill(row, gx + 3, gx + 4, P_TUFT);
      }
      break;
    case Terrain::Forest: {
      fill(row, x0, x0 + T, P_FOREST_GND);
      const int32_t ccx = x0 + T / 2 + (int32_t)(d % 5) - 2;   // jittered tree centre
      const int32_t h = s_canopyHalf[iy];
      if (h) fill(row, ccx - h, ccx + h, P_CANOPY);
      const int32_t hh = s_canopyHiHalf[iy];
      if (hh) fill(row, ccx - 3 - hh, ccx - 3 + hh, P_CANOPY_HI);
      break;
    }
    case Terrain::Rock:
      fill(row, x0, x0 + T, P_ROCK[alt]);
      if (iy >= 6 + (d & 3) && iy <= 9 + (d & 3)) {
        const int32_t rx = x0 + 4 + (d % 9);
        fill(row, rx, rx + 7, P_ROCK_DARK);
      }
      break;
    case Terrain::Trail:
      fill(row, x0, x0 + T, P_TRAIL[alt]);
      if ((iy == 3 + (d & 3)) || (iy == 15 + ((d >> 2) & 3)))
        fill(row, x0 + 2 + (d % 15), x0 + 4 + (d % 15), P_TRAIL_EDGE);
      break;
  }
}

void rebuildTileCache() {
  if (d_firstTx == c_firstTx && d_firstTy == c_firstTy) return;
  for (int r = 0; r < ROWS; ++r)
    for (int c = 0; c < COLS; ++c)
      s_tiles[r][c] = world::terrainAt(d_firstTx + c, d_firstTy + r);
  c_firstTx = d_firstTx;
  c_firstTy = d_firstTy;
}

void worldToScreen(const locator::Pos& p, int32_t& sx, int32_t& sy) {
  float dx, dy;
  locator::deltaM(d_pos, p, dx, dy);
  sx = PLAYER_SX + (int32_t)lroundf(dx * PX_PER_M);
  sy = PLAYER_SY + (int32_t)lroundf(dy * PX_PER_M);
}

// ---- Entity art (library primitives, native RGB565) ----
void drawEnemy(lgfx::LGFX_Sprite& b, int32_t bandY, const EnemyDraw& e) {
  const int32_t bob = (int32_t)(sinf(d_time * 4.0f + e.phase) * 2.0f);
  const int32_t x = e.x, y = e.y + bob;
  if (!hud::rowsHit(y - 64, y + 64, bandY, b.height())) return;
  const int32_t by = y - bandY;

  if (e.target) {                                       // engage radius ring, pulsing
    const int32_t r = (int32_t)(OW_ENGAGE_RADIUS_M * PX_PER_M);
    const uint16_t rc = ((int32_t)(d_time * 4.0f) & 1) ? rgb565(255, 230, 60) : rgb565(255, 160, 40);
    b.drawCircle(x, e.y - bandY, r, rc);
    b.drawCircle(x, e.y - bandY, r - 1, rc);
  }
  b.fillEllipse(x, e.y + 9 - bandY, 10, 3, rgb565(20, 30, 20));        // ground shadow

  switch ((world::EnemyKind)e.kind) {
    case world::EnemyKind::Shade:                       // purple ghost
      b.fillCircle(x, by - 3, 9, rgb565(120, 60, 170));
      b.fillRect(x - 9, by - 3, 19, 10, rgb565(120, 60, 170));
      b.fillRect(x - 9, by + 5, 4, 3, rgb565(120, 60, 170));
      b.fillRect(x + 6, by + 5, 4, 3, rgb565(120, 60, 170));
      b.fillRect(x - 5, by - 6, 3, 4, rgb565(255, 255, 255));
      b.fillRect(x + 3, by - 6, 3, 4, rgb565(255, 255, 255));
      break;
    case world::EnemyKind::Brute:                       // red horned block
      b.fillRoundRect(x - 11, by - 10, 22, 20, 5, rgb565(180, 40, 40));
      b.fillTriangle(x - 11, by - 8, x - 6, by - 10, x - 13, by - 17, rgb565(240, 220, 180));
      b.fillTriangle(x + 11, by - 8, x + 6, by - 10, x + 13, by - 17, rgb565(240, 220, 180));
      b.fillRect(x - 6, by - 4, 4, 3, rgb565(255, 220, 0));
      b.fillRect(x + 3, by - 4, 4, 3, rgb565(255, 220, 0));
      break;
    case world::EnemyKind::Phantom: {                   // rare: glowing cyan wisp
      b.fillCircle(x, by - 2, 11, rgb565(60, 190, 220));
      b.fillCircle(x - 3, by - 5, 5, rgb565(200, 250, 255));
      const int32_t s = ((int32_t)(d_time * 8.0f + e.phase)) % 4;
      b.fillRect(x + 12 + s, by - 12 - s, 2, 2, rgb565(255, 255, 255));   // sparkle
      break;
    }
  }
}

void drawPlayer(lgfx::LGFX_Sprite& b, int32_t bandY) {
  const int32_t x = PLAYER_SX, y = PLAYER_SY;
  if (!hud::rowsHit(y - 20, y + 20, bandY, b.height())) return;
  const int32_t by = y - bandY;
  const float h = locator::headingRad();
  const float hx = cosf(h), hy = sinf(h);
  if (s_move == locator::Move::Run) {                   // speed streaks behind
    for (int i = 1; i <= 3; ++i)
      b.fillRect(x - (int32_t)(hx * (10 + i * 5)) - 1, by - (int32_t)(hy * (10 + i * 5)) - 1,
                 3, 3, rgb565(230, 240, 255));
  }
  b.fillEllipse(x, by + 8, 10, 4, rgb565(20, 30, 20));
  b.fillCircle(x, by, 9, rgb565(30, 170, 80));                         // jersey
  b.fillCircle(x, by, 9 - 6, rgb565(255, 255, 255));                   // bib
  b.fillCircle(x + (int32_t)(hx * 6), by + (int32_t)(hy * 6), 5, rgb565(240, 196, 150));   // head
  b.fillCircle(x + (int32_t)(hx * 7), by + (int32_t)(hy * 7), 3, rgb565(110, 60, 20));     // hair
}

} // namespace

namespace overworld {

bool init() {
  using renderer::raw;
  // Alternate-tile shading only on grass, one green step: anything stronger reads as a
  // chessboard (host preview, R3).
  P_WATER[0] = P_WATER[1] = raw(rgb565(40, 90, 170));
  P_WATER_HI = raw(rgb565(150, 200, 240));
  P_SAND[0] = P_SAND[1] = raw(rgb565(220, 200, 140));
  P_SAND_DOT = raw(rgb565(180, 160, 110));
  P_GRASS[0] = raw(rgb565(80, 160, 70));  P_GRASS[1] = raw(rgb565(80, 156, 70));
  P_TUFT = raw(rgb565(50, 120, 45));
  P_FLOWER[0] = raw(rgb565(250, 240, 120)); P_FLOWER[1] = raw(rgb565(250, 140, 170));
  P_FLOWER[2] = raw(rgb565(240, 240, 250));
  P_FOREST_GND = raw(rgb565(52, 112, 50));
  P_CANOPY = raw(rgb565(28, 84, 40));     P_CANOPY_HI = raw(rgb565(48, 116, 56));
  P_ROCK[0] = P_ROCK[1] = raw(rgb565(130, 128, 124));
  P_ROCK_DARK = raw(rgb565(92, 90, 88));
  P_TRAIL[0] = P_TRAIL[1] = raw(rgb565(176, 140, 96));
  P_TRAIL_EDGE = raw(rgb565(140, 108, 72));

  const float r = OW_TILE_PX * 0.42f, rh = OW_TILE_PX * 0.18f;
  const float cy = OW_TILE_PX * 0.5f;
  for (int iy = 0; iy < OW_TILE_PX; ++iy) {
    const float dy = iy + 0.5f - cy;
    s_canopyHalf[iy] = (fabsf(dy) < r) ? (uint8_t)sqrtf(r * r - dy * dy) : 0;
    const float dh = iy + 0.5f - (cy - 3.0f);
    s_canopyHiHalf[iy] = (fabsf(dh) < rh) ? (uint8_t)sqrtf(rh * rh - dh * dh) : 0;
  }
  s_cur = s_prev = locator::pos();
  return true;
}

void update(float dt) {
  s_prev = s_cur;
  s_cur = locator::pos();
  s_move = locator::move();
  const float v = locator::speedMS();
  const bool running = (s_move == locator::Move::Run);

  if (s_move != locator::Move::Still) {
    s_walked += v * dt;                                   // real-world metres
    s_needMove = false;
  }
  if (running) {
    s_energy += v * dt * ENERGY_PER_M_RUN;
    if (s_energy > ENERGY_MAX) s_energy = ENERGY_MAX;
  }

  // Nearby enemies + the engage candidate (nearest within range).
  s_nEnemies = world::enemiesNear(s_cur, 14, running, s_enemies, MAX_ENEMIES);
  s_target = -1;
  float best = OW_ENGAGE_RADIUS_M;
  for (int i = 0; i < s_nEnemies; ++i) {
    float dx, dy;
    locator::deltaM(s_cur, s_enemies[i].pos, dx, dy);
    const float d = sqrtf(dx * dx + dy * dy);
    if (d < best) { best = d; s_target = i; }
  }

  if (s_target >= 0 && !s_needMove && s_move == locator::Move::Still) {
    s_hold += dt;
    if (s_hold >= OW_STILL_HOLD_S) {
      s_engaged = true;
      s_engagedEnemy = s_enemies[s_target];
      s_hold = 0.0f;
    }
  } else {
    s_hold = 0.0f;
  }

  const input::State& in = input::state();
  if (in.pressed && in.pointX >= MENU_X && in.pointX < MENU_X + MENU_W &&
      in.pointY >= 0 && in.pointY < MENU_Y + MENU_H + 6)
    s_menuReq = true;
}

void beginRender(float alpha) {
  if (alpha < 0) alpha = 0;
  if (alpha > 1) alpha = 1;
  float dx, dy;
  locator::deltaM(s_prev, s_cur, dx, dy);
  d_pos = s_prev;
  d_pos.fx += dx * alpha;
  d_pos.fy += dy * alpha;
  while (d_pos.fx >= OW_TILE_M) { d_pos.fx -= OW_TILE_M; ++d_pos.tx; }
  while (d_pos.fx < 0.0f)       { d_pos.fx += OW_TILE_M; --d_pos.tx; }
  while (d_pos.fy >= OW_TILE_M) { d_pos.fy -= OW_TILE_M; ++d_pos.ty; }
  while (d_pos.fy < 0.0f)       { d_pos.fy += OW_TILE_M; --d_pos.ty; }
  d_time = millis() * 0.001f;

  // Camera in world pixels. Tile index * px fits int32 for any GPS coordinate.
  d_camX = d_pos.tx * OW_TILE_PX + (int32_t)(d_pos.fx * PX_PER_M) - PLAYER_SX;
  d_camY = d_pos.ty * OW_TILE_PX + (int32_t)(d_pos.fy * PX_PER_M) - PLAYER_SY;
  d_firstTx = floorDiv(d_camX, OW_TILE_PX);
  d_firstTy = floorDiv(d_camY, OW_TILE_PX);
  d_offX = d_camX - d_firstTx * OW_TILE_PX;
  d_offY = d_camY - d_firstTy * OW_TILE_PX;
  rebuildTileCache();

  s_nEdraw = 0;
  for (int i = 0; i < s_nEnemies; ++i) {
    int32_t sx, sy;
    worldToScreen(s_enemies[i].pos, sx, sy);
    if (sx < -70 || sx > LCD_WIDTH + 70 || sy < -70 || sy > LCD_HEIGHT + 70) continue;
    s_edraw[s_nEdraw++] = { (int16_t)sx, (int16_t)sy, (uint8_t)s_enemies[i].kind,
                            i == s_target, (uint8_t)(s_enemies[i].cx * 7 + s_enemies[i].cy * 13) };
  }
}

void composeTerrain(lgfx::LGFX_Sprite& band, int32_t bandY) {
  uint16_t* buf = (uint16_t*)band.getBuffer();
  const int32_t h = band.height();
  const int32_t wave = (int32_t)(d_time * 6.0f);
  for (int32_t r = 0; r < h; ++r) {
    const int32_t y = bandY + r;
    if (y >= LCD_HEIGHT) break;
    uint16_t* row = buf + r * LCD_WIDTH;
    const int32_t ty = y + d_offY;
    const int32_t tr = ty / OW_TILE_PX, iy = ty % OW_TILE_PX;
    for (int c = 0; c < COLS; ++c) {
      const int32_t x0 = c * OW_TILE_PX - d_offX;
      if (x0 >= LCD_WIDTH) break;
      tileRow(row, x0, iy, s_tiles[tr][c], d_firstTx + c, d_firstTy + tr, wave);
    }
  }
}

void composeEntities(lgfx::LGFX_Sprite& band, int32_t bandY) {
  // Enemies behind (above) the player first, then the player, then those in front.
  for (int i = 0; i < s_nEdraw; ++i) if (s_edraw[i].y <= PLAYER_SY) drawEnemy(band, bandY, s_edraw[i]);
  drawPlayer(band, bandY);
  for (int i = 0; i < s_nEdraw; ++i) if (s_edraw[i].y > PLAYER_SY) drawEnemy(band, bandY, s_edraw[i]);

  // Name tag over the engage candidate.
  for (int i = 0; i < s_nEdraw; ++i) {
    if (!s_edraw[i].target) continue;
    hud::text(band, bandY, world::name((world::EnemyKind)s_edraw[i].kind), s_edraw[i].x,
              s_edraw[i].y - 26, hud::F9, 1.0f, rgb565(255, 230, 60), 160);
  }
}

void composeHud(lgfx::LGFX_Sprite& band, int32_t bandY) {
  using hud::rect; using hud::text;
  char buf[48];
  const uint16_t BAR = rgb565(24, 28, 32), WHITE = rgb565(255, 255, 255);
  const uint16_t GREY = rgb565(190, 190, 190), YELLOW = rgb565(255, 220, 40);

  // Top bar: MENU | Run energy + shield pips | move state | coins.
  rect(band, bandY, 0, 0, LCD_WIDTH, TOP_BAR_H, BAR);
  rect(band, bandY, MENU_X, MENU_Y, MENU_W, MENU_H, rgb565(52, 56, 64));
  hud::frame(band, bandY, MENU_X, MENU_Y, MENU_W, MENU_H, GREY);
  text(band, bandY, "MENU", MENU_X + MENU_W / 2, MENU_Y + MENU_H / 2, hud::F9, 1.0f, WHITE);

  text(band, bandY, "RUN", 108, 16, hud::F9, 1.0f, GREY);
  rect(band, bandY, 128, 9, 124, 14, rgb565(60, 60, 60));
  rect(band, bandY, 130, 11, (int32_t)(120 * s_energy / ENERGY_MAX), 10, YELLOW);
  rect(band, bandY, 130 + 60, 9, 1, 14, BAR);                       // shield threshold tick
  const int shields = (int)(s_energy / ENERGY_PER_SHIELD);
  for (int i = 0; i < shields && i < 2; ++i)
    rect(band, bandY, 258 + i * 14, 10, 10, 12, rgb565(90, 200, 255));

  const char* mv = s_move == locator::Move::Run ? "RUN" : (s_move == locator::Move::Walk ? "WALK" : "STILL");
  const uint16_t mc = s_move == locator::Move::Run ? rgb565(255, 140, 60)
                    : (s_move == locator::Move::Walk ? rgb565(120, 220, 120) : GREY);
  text(band, bandY, mv, 330, 16, hud::F9, 1.0f, mc);

  if (hud::rowsHit(6, 26, bandY, band.height())) {
    band.fillEllipse(400, 16 - bandY, 7, 8, rgb565(240, 180, 20));
    band.fillEllipse(398, 13 - bandY, 3, 3, rgb565(255, 240, 150));
  }
  snprintf(buf, sizeof buf, "%u", (unsigned)s_wallet);
  text(band, bandY, buf, 440, 16, hud::F12, 1.0f, YELLOW, 70);

  // Bottom banner: engage prompt, or the movement hint.
  const int32_t BY = LCD_HEIGHT - 34;
  if (s_target >= 0) {
    const world::Enemy& e = s_enemies[s_target];
    rect(band, bandY, 0, BY, LCD_WIDTH, 34, rgb565(60, 20, 20));
    if (s_needMove)
      snprintf(buf, sizeof buf, "Move away and come back to face the %s", world::name(e.kind));
    else if (s_move == locator::Move::Still)
      snprintf(buf, sizeof buf, "Hold still... the %s is coming!", world::name(e.kind));
    else
      snprintf(buf, sizeof buf, "%s nearby - STOP to engage", world::name(e.kind));
    text(band, bandY, buf, LCD_WIDTH / 2, BY + 14, hud::F12, 1.0f, WHITE);
    if (s_hold > 0.0f)
      rect(band, bandY, 0, LCD_HEIGHT - 4, (int32_t)(LCD_WIDTH * s_hold / OW_STILL_HOLD_S), 4,
           rgb565(255, 80, 60));
  } else {
    text(band, bandY, "Drag to walk  -  drag further to run", LCD_WIDTH / 2, LCD_HEIGHT - 14,
         hud::F9, 1.0f, WHITE);
  }
}

bool takeEngagement(world::Enemy& out) {
  if (!s_engaged) return false;
  s_engaged = false;
  out = s_engagedEnemy;
  return true;
}

void requireMoveBeforeEngage() { s_needMove = true; s_hold = 0.0f; }

bool takeMenuRequest() {
  const bool r = s_menuReq;
  s_menuReq = false;
  return r;
}

float energy()            { return s_energy; }
void  setEnergy(float e)  { s_energy = e < 0 ? 0 : (e > ENERGY_MAX ? ENERGY_MAX : e); }
float walkedM()           { return s_walked; }
void  setWalkedM(float m) { s_walked = m; }
void  setStats(uint32_t wallet, uint32_t escapes) { s_wallet = wallet; s_escapes = escapes; }

} // namespace overworld
