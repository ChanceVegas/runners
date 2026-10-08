// overworld.cpp — top-down map. Camera keeps the player at (OW_PLAYER_SX, SY). The
// visible tiles' terrain is cached and only recomputed when the view crosses a tile
// boundary. Terrain rows are copied from 24x24 pixel-art tiles (A3, art_data.h)
// straight into the SRAM band buffer through the raw palette, so a full-screen map
// costs a few ms, not thousands of calls. Avatar and enemies are sprites.
#include "overworld.h"
#include "locator.h"
#include "renderer.h"
#include "input.h"
#include "hud.h"
#include "sprite.h"
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

inline int32_t floorDiv(int32_t a, int32_t b) {
  int32_t q = a / b;
  if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
  return q;
}


// A3: terrain is 24x24 pixel-art tiles (art_data.h), one opaque row copied per tile
// per screen row through the shared palette. Same cost as the old flat fills: one
// lookup + one store per pixel. Variants come from the tile's deco hash; water swaps
// between two frames, offset per tile so the lake shimmers instead of blinking.
static_assert(OW_TILE_PX == 24, "map tiles are drawn 1:1 at 24 px");

const ArtSprite& tileArt(const world::Tile& t, int32_t tx, int32_t ty, int32_t waveFrame) {
  const uint8_t d = t.deco;
  switch (t.terrain) {
    case Terrain::Water:  return ((waveFrame + ((tx + ty) & 1)) & 1) ? ART_T_WATER1 : ART_T_WATER0;
    case Terrain::Sand:   return (d & 3) == 0 ? ART_T_SAND1 : ART_T_SAND0;
    case Terrain::Forest: return (d & 1) ? ART_T_FOREST1 : ART_T_FOREST0;
    case Terrain::Rock:   return (d % 3) == 0 ? ART_T_ROCK1 : ART_T_ROCK0;
    case Terrain::Trail:  return (d & 1) ? ART_T_TRAIL1 : ART_T_TRAIL0;
    case Terrain::Grass:
    default:              return (d % 5) == 0 ? ART_T_GRASS1 : ((d % 3) == 1 ? ART_T_GRASS2 : ART_T_GRASS0);
  }
}

inline void tileRow(uint16_t* row, int32_t x0, int32_t iy, const world::Tile& t,
                    int32_t tx, int32_t ty, int32_t waveFrame, const uint16_t* pal) {
  const ArtSprite& a = tileArt(t, tx, ty, waveFrame);
  const uint8_t* src = a.px + iy * OW_TILE_PX;
  int32_t i0 = 0, i1 = OW_TILE_PX;
  if (x0 < 0) i0 = -x0;
  if (x0 + i1 > LCD_WIDTH) i1 = LCD_WIDTH - x0;
  uint16_t* dst = row + x0;
  for (int32_t i = i0; i < i1; ++i) dst[i] = pal[src[i]];
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
  b.fillEllipse(x, e.y + 11 - bandY, 11, 3, rgb565(20, 30, 20));       // ground shadow
  // A3: the same front-view art as the battle pursuer, at map size (~25x29).
  const ArtSprite& a = e.kind == (uint8_t)world::EnemyKind::Shade ? ART_SHADE_F
                     : e.kind == (uint8_t)world::EnemyKind::Brute ? ART_BRUTE_F : ART_PHANTOM_F;
  const int32_t w = a.w * 9 / 10, h = a.h * 9 / 10;
  sprite::draw(b, bandY, a, x - w / 2, y + 12 - h, w, h);
  if (e.kind == (uint8_t)world::EnemyKind::Phantom) {   // rare: sparkle
    const int32_t sp = ((int32_t)(d_time * 8.0f + e.phase)) % 4;
    b.fillRect(x + 13 + sp, by - 14 - sp, 2, 2, rgb565(255, 255, 255));
  }
}

void drawPlayer(lgfx::LGFX_Sprite& b, int32_t bandY) {
  const int32_t x = PLAYER_SX, y = PLAYER_SY;
  if (!hud::rowsHit(y - 26, y + 14, bandY, b.height())) return;
  const int32_t by = y - bandY;
  const float h = locator::headingRad();
  const float hx = cosf(h), hy = sinf(h);
  if (s_move == locator::Move::Run) {                   // speed streaks behind
    for (int i = 1; i <= 3; ++i)
      b.fillRect(x - (int32_t)(hx * (10 + i * 5)) - 1, by - (int32_t)(hy * (10 + i * 5)) - 1,
                 3, 3, rgb565(230, 240, 255));
  }
  b.fillEllipse(x, by + 9, 10, 4, rgb565(20, 30, 20));
  // A3 avatar (12x15 art at 2x): facing from the heading, 2-step walk while moving.
  const bool moving = s_move != locator::Move::Still;
  const bool step = moving && (((int32_t)(d_time * (s_move == locator::Move::Run ? 8.0f : 5.0f))) & 1);
  const ArtSprite* a;
  bool flip = false;
  if (fabsf(hx) > fabsf(hy)) { a = step ? &ART_AV_SIDE1 : &ART_AV_SIDE0; flip = hx < 0.0f; }
  else if (hy > 0.0f)        { a = step ? &ART_AV_DOWN1 : &ART_AV_DOWN0; }
  else                       { a = step ? &ART_AV_UP1 : &ART_AV_UP0; }
  sprite::drawBottom(b, bandY, *a, x, y + 11, 2.0f, flip);
}

} // namespace

namespace overworld {

bool init() {
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
  const int32_t waveFrame = (int32_t)(d_time * 2.0f);    // water: 2 frames, 2 Hz
  const uint16_t* pal = sprite::rawPalette();
  for (int32_t r = 0; r < h; ++r) {
    const int32_t y = bandY + r;
    if (y >= LCD_HEIGHT) break;
    uint16_t* row = buf + r * LCD_WIDTH;
    const int32_t ty = y + d_offY;
    const int32_t tr = ty / OW_TILE_PX, iy = ty % OW_TILE_PX;
    for (int c = 0; c < COLS; ++c) {
      const int32_t x0 = c * OW_TILE_PX - d_offX;
      if (x0 >= LCD_WIDTH) break;
      tileRow(row, x0, iy, s_tiles[tr][c], d_firstTx + c, d_firstTy + tr, waveFrame, pal);
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
    sprite::draw(band, bandY, ART_SHIELD, 258 + i * 16, 5, 12, 13);

  const char* mv = s_move == locator::Move::Run ? "RUN" : (s_move == locator::Move::Walk ? "WALK" : "STILL");
  const uint16_t mc = s_move == locator::Move::Run ? rgb565(255, 140, 60)
                    : (s_move == locator::Move::Walk ? rgb565(120, 220, 120) : GREY);
  text(band, bandY, mv, 330, 16, hud::F9, 1.0f, mc);

  sprite::draw(band, bandY, ART_COIN, 392, 8, 16, 16);
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
