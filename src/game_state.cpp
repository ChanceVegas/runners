// game_state.cpp — see game_state.h. Flash writes (Preferences "profile") happen only
// at mode changes and every PROFILE_SAVE_S in Explore — never per frame.
#include "game_state.h"
#include "renderer.h"
#include "input.h"
#include "locator.h"
#include "world.h"
#include "overworld.h"
#include "lanes.h"
#include "scenery.h"
#include "encounter.h"
#include "hud.h"
#include "color.h"
#include "config.h"
#include "board_config.h"
#include <Arduino.h>
#include <Preferences.h>
#include <stdio.h>

namespace {

using game_state::Mode;

constexpr float PROFILE_SAVE_S = 120.0f;   // periodic save while exploring

// Menu buttons (screen rects).
constexpr int32_t BTN_Y = 128, BTN_H = 62, BTN_W = 190;
constexpr int32_t BTN_EXPLORE_X = 30, BTN_ARCADE_X = LCD_WIDTH - 30 - BTN_W;

Mode s_mode = Mode::Menu;
Preferences s_prefs;
uint32_t s_wallet = 0, s_escapes = 0;
float s_saveT = 0.0f;
float s_menuT = 0.0f;                      // ignore taps right after entering the menu

world::Enemy s_chaseEnemy;
uint8_t s_chaseShields = 0;

void layersEncounter() {
  renderer::clearLayers();
  renderer::addLayer(lanes::composeBand);
  renderer::addLayer(scenery::composeBand);
  renderer::addLayer(encounter::composeObjects);
  renderer::addLayer(encounter::composeHud);
}

void layersMenu() {
  layersEncounter();                       // attract road behind the menu
  renderer::addLayer(game_state::composeMenu);
}

void layersExplore() {
  renderer::clearLayers();
  renderer::addLayer(overworld::composeTerrain);
  renderer::addLayer(overworld::composeEntities);
  renderer::addLayer(overworld::composeHud);
}

void saveProfile() {
  const locator::Pos p = locator::pos();
  s_prefs.putUInt("wallet", s_wallet);
  s_prefs.putUInt("escapes", s_escapes);
  s_prefs.putFloat("energy", overworld::energy());
  s_prefs.putFloat("walked", overworld::walkedM());
  s_prefs.putInt("tx", p.tx);
  s_prefs.putInt("ty", p.ty);
  s_saveT = 0.0f;
}

void syncHudStats() { overworld::setStats(s_wallet, s_escapes); }

// First boot: spiral out from the origin to the nearest land tile.
locator::Pos findLand(int32_t tx, int32_t ty) {
  for (int32_t r = 0; r < 60; ++r)
    for (int32_t dy = -r; dy <= r; ++dy)
      for (int32_t dx = -r; dx <= r; ++dx) {
        if (abs(dx) != r && abs(dy) != r) continue;
        const world::Terrain t = world::terrainAt(tx + dx, ty + dy).terrain;
        if (t != world::Terrain::Water && t != world::Terrain::Rock)
          return { tx + dx, ty + dy, OW_TILE_M * 0.5f, OW_TILE_M * 0.5f };
      }
  return { tx, ty, OW_TILE_M * 0.5f, OW_TILE_M * 0.5f };
}

void enterMenu() {
  locator::setEnabled(false);
  encounter::idle();
  layersMenu();
  s_menuT = 0.0f;
  s_mode = Mode::Menu;
}

void enterExplore() {
  encounter::idle();
  syncHudStats();
  layersExplore();
  locator::setEnabled(true);
  s_mode = Mode::Explore;
}

void enterChase(const world::Enemy& e) {
  s_chaseEnemy = e;
  // Shields: one per ENERGY_PER_SHIELD of Run energy, max 2, paid up front; unused
  // ones are refunded after the chase.
  int sh = (int)(overworld::energy() / ENERGY_PER_SHIELD);
  if (sh > 2) sh = 2;
  s_chaseShields = (uint8_t)sh;
  overworld::setEnergy(overworld::energy() - sh * ENERGY_PER_SHIELD);
  saveProfile();
  locator::setEnabled(false);
  layersEncounter();
  encounter::startChase(world::level(e.kind), world::goalM(e.kind), s_chaseShields,
                        world::name(e.kind));
  Serial.printf("[game] chase: %s level %u goal %d m shields %u\n", world::name(e.kind),
                (unsigned)world::level(e.kind), (int)world::goalM(e.kind), (unsigned)s_chaseShields);
  s_mode = Mode::Chase;
}

void finishChase() {
  const encounter::Result r = encounter::result();
  const uint8_t unused = (r.shieldsUsed < s_chaseShields) ? s_chaseShields - r.shieldsUsed : 0;
  overworld::setEnergy(overworld::energy() + unused * ENERGY_PER_SHIELD);
  s_wallet += r.coins;
  if (r.won) {
    s_wallet += ESCAPE_BONUS_COINS * world::level(s_chaseEnemy.kind);
    ++s_escapes;
    world::markEscaped(s_chaseEnemy);
  }
  overworld::requireMoveBeforeEngage();
  Serial.printf("[game] chase %s: +%u coins, shields used %u, wallet %u\n",
                r.won ? "ESCAPED" : "CAUGHT", (unsigned)r.coins, (unsigned)r.shieldsUsed,
                (unsigned)s_wallet);
  saveProfile();
  enterExplore();
}

bool tapIn(const input::State& in, int32_t x, int32_t y, int32_t w, int32_t h) {
  return in.pressed && in.pointX >= x && in.pointX < x + w && in.pointY >= y && in.pointY < y + h;
}

} // namespace

namespace game_state {

bool init() {
  s_prefs.begin("profile", false);
  s_wallet = s_prefs.getUInt("wallet", 0);
  s_escapes = s_prefs.getUInt("escapes", 0);
  overworld::setEnergy(s_prefs.getFloat("energy", 0.0f));
  overworld::setWalkedM(s_prefs.getFloat("walked", 0.0f));
  locator::Pos start;
  if (s_prefs.isKey("tx")) {
    start = { s_prefs.getInt("tx", 0), s_prefs.getInt("ty", 0), OW_TILE_M * 0.5f, OW_TILE_M * 0.5f };
  } else {
    start = findLand(0, 0);
  }
  locator::init(start);
  overworld::init();
  Serial.printf("[game] profile: wallet %u escapes %u energy %d start tile %d,%d\n",
                (unsigned)s_wallet, (unsigned)s_escapes, (int)overworld::energy(),
                (int)start.tx, (int)start.ty);
  enterMenu();
  return true;
}

void update(float dt) {
  const input::State& in = input::state();
  switch (s_mode) {
    case Mode::Menu:
      encounter::update(dt);                  // attract road
      s_menuT += dt;
      if (s_menuT < 0.4f) break;
      if (tapIn(in, BTN_EXPLORE_X, BTN_Y, BTN_W, BTN_H)) enterExplore();
      else if (tapIn(in, BTN_ARCADE_X, BTN_Y, BTN_W, BTN_H)) {
        layersEncounter();
        encounter::startArcade();
        s_mode = Mode::Arcade;
      }
      break;

    case Mode::Explore: {
      locator::update(dt);
      overworld::update(dt);
      s_saveT += dt;
      if (s_saveT >= PROFILE_SAVE_S) saveProfile();
      world::Enemy e;
      if (overworld::takeMenuRequest()) { saveProfile(); enterMenu(); }
      else if (overworld::takeEngagement(e)) enterChase(e);
      break;
    }

    case Mode::Chase:
      encounter::update(dt);
      if (encounter::finished()) finishChase();
      break;

    case Mode::Arcade:
      encounter::update(dt);
      if (encounter::finished() && encounter::result().exitToMenu) enterMenu();
      break;
  }
}

void beginRender(float alpha) {
  if (s_mode == Mode::Explore) overworld::beginRender(alpha);
  else encounter::beginRender(alpha);
}

Mode mode() { return s_mode; }

const char* modeName() {
  switch (s_mode) {
    case Mode::Menu:    return "MENU";
    case Mode::Explore: return "EXPLORE";
    case Mode::Chase:   return "CHASE";
    case Mode::Arcade:  return "ARCADE";
  }
  return "?";
}

void composeMenu(lgfx::LGFX_Sprite& b, int32_t bandY) {
  using hud::text; using hud::rect; using hud::frame;
  char buf[64];
  const int32_t cx = LCD_WIDTH / 2;
  const uint16_t WHITE = rgb565(255, 255, 255), YELLOW = rgb565(255, 220, 40);

  text(b, bandY, "RUNNERS", cx, 50, hud::F24, 1.4f, YELLOW);

  rect(b, bandY, BTN_EXPLORE_X, BTN_Y, BTN_W, BTN_H, rgb565(30, 110, 60));
  frame(b, bandY, BTN_EXPLORE_X, BTN_Y, BTN_W, BTN_H, WHITE);
  text(b, bandY, "EXPLORE", BTN_EXPLORE_X + BTN_W / 2, BTN_Y + 22, hud::F18, 1.0f, WHITE, BTN_W - 12);
  text(b, bandY, "map + chases", BTN_EXPLORE_X + BTN_W / 2, BTN_Y + 48, hud::F9, 1.0f, WHITE);

  rect(b, bandY, BTN_ARCADE_X, BTN_Y, BTN_W, BTN_H, rgb565(150, 70, 20));
  frame(b, bandY, BTN_ARCADE_X, BTN_Y, BTN_W, BTN_H, WHITE);
  text(b, bandY, "ARCADE", BTN_ARCADE_X + BTN_W / 2, BTN_Y + 22, hud::F18, 1.0f, WHITE, BTN_W - 12);
  text(b, bandY, "endless run", BTN_ARCADE_X + BTN_W / 2, BTN_Y + 48, hud::F9, 1.0f, WHITE);

  snprintf(buf, sizeof buf, "Coins %u   Escapes %u   Best %u", (unsigned)s_wallet,
           (unsigned)s_escapes, (unsigned)encounter::bestScore());
  text(b, bandY, buf, cx, 222, hud::F12, 1.0f, WHITE);
  snprintf(buf, sizeof buf, "Walked %d m", (int)overworld::walkedM());
  text(b, bandY, buf, cx, 252, hud::F9, 1.0f, rgb565(200, 200, 200));
}

} // namespace game_state
