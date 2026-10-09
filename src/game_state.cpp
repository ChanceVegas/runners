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
#include "audio.h"
#include "shop.h"
#include "settings.h"
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

// Menu buttons (screen rects): EXPLORE | SHOP | ARCADE.
constexpr int32_t BTN_Y = 118, BTN_H = 70, BTN_W = 144;
constexpr int32_t BTN_EXPLORE_X = 12, BTN_SHOP_X = 168, BTN_ARCADE_X = 324;
constexpr int32_t BTN_SET_X = 360, BTN_SET_Y = 8, BTN_SET_W = 112, BTN_SET_H = 40;   // SETTINGS

Mode s_mode = Mode::Menu;
Preferences s_prefs;
uint32_t s_wallet = 0, s_escapes = 0;     // escapes = battles won (escaped or defeated)
uint32_t s_lost = 0, s_gotAway = 0;
uint32_t s_xp = 0;                        // runner XP (battle::levelOf -> level); never lost
float s_saveT = 0.0f;
float s_menuT = 0.0f;                      // ignore taps right after entering the menu

world::Enemy s_battleEnemy;
uint8_t s_battleShields = 0;
uint8_t s_energyShields = 0;              // of s_battleShields, bought with Run energy (refundable)
int8_t  s_dropItem = -1;                  // drink the enemy drops if DEFEATED (pre-rolled)

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

void layersShop() {
  renderer::clearLayers();
  renderer::addLayer(shop::composeShop);   // full-screen panel
}

void layersSettings() {
  renderer::clearLayers();
  renderer::addLayer(settings::compose);
}

void layersPreBattle() {
  renderer::clearLayers();
  renderer::addLayer(shop::composePreBattle);
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
  s_prefs.putUInt("lost", s_lost);
  s_prefs.putUInt("gotaway", s_gotAway);
  s_prefs.putUInt("xp", s_xp);
  s_prefs.putFloat("energy", overworld::energy());
  s_prefs.putFloat("walked", overworld::walkedM());
  s_prefs.putInt("tx", p.tx);
  s_prefs.putInt("ty", p.ty);
  shop::save(s_prefs);
  settings::save(s_prefs);
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

void enterBattle(const world::Enemy& e) {
  s_battleEnemy = e;
  // Shields: one per ENERGY_PER_SHIELD of Run energy, max 2, paid up front; unused
  // ones are refunded after the battle. A GUARD drink adds one more (not refunded).
  int sh = (int)(overworld::energy() / ENERGY_PER_SHIELD);
  if (sh > 2) sh = 2;
  s_energyShields = (uint8_t)sh;
  overworld::setEnergy(overworld::energy() - sh * ENERGY_PER_SHIELD);
  uint8_t guard = 0;
  const battle::Stats stats = shop::takeBattleStats(guard);   // sneakers + chosen drinks
  s_battleShields = s_energyShields + guard;
  s_dropItem = shop::rollDefeatDrop();
  saveProfile();                              // drinks used are gone even if power drops
  locator::setEnabled(false);
  layersEncounter();
  encounter::setDefeatDrop(s_dropItem >= 0 ? shop::name((shop::Item)s_dropItem) : nullptr);
  encounter::startBattle((uint8_t)e.kind, world::level(e.kind), world::goalM(e.kind),
                         s_battleShields, stats, s_xp);
  Serial.printf("[game] battle: %s level %u player L%u goal %d m shields %u (guard %u) gain+%.2f jump x%.2f "
                "grip x%.2f orb %u rush %d\n", world::name(e.kind), (unsigned)world::level(e.kind),
                (unsigned)battle::levelOf(s_xp),
                (int)world::goalM(e.kind), (unsigned)s_battleShields, (unsigned)guard,
                stats.gapGainBonus, stats.jumpMul, stats.gapLossMul, (unsigned)stats.orbPower,
                (int)stats.startGapBonus);
  s_mode = Mode::Battle;
}

// Engaged on the map: drink screen first if the player carries any, else straight in.
void engage(const world::Enemy& e) {
  if (!shop::hasDrinks()) { enterBattle(e); return; }
  s_battleEnemy = e;
  locator::setEnabled(false);
  shop::openPreBattle(world::name(e.kind), world::level(e.kind));
  layersPreBattle();
  s_mode = Mode::PreBattle;
}

void enterShop() {
  shop::openShop();
  layersShop();
  s_mode = Mode::Shop;
}

void finishBattle() {
  const encounter::Result r = encounter::result();
  uint8_t unused = (r.shieldsUsed < s_battleShields) ? s_battleShields - r.shieldsUsed : 0;
  if (unused > s_energyShields) unused = s_energyShields;   // the GUARD shield isn't energy
  overworld::setEnergy(overworld::energy() + unused * ENERGY_PER_SHIELD);
  if (r.won) s_wallet += r.coins;              // lost (caught / got away): the battle's coins are lost
  if (!r.won && !r.defeated) { if (r.gotAway) ++s_gotAway; else ++s_lost; }
  if (r.won || r.defeated) {                    // defeated = passed it (kept even if caught later)
    const uint8_t lv = world::level(s_battleEnemy.kind);
    s_wallet += (uint32_t)((r.defeated ? DEFEAT_BONUS_COINS : ESCAPE_BONUS_COINS) * lv *
                           battle::rewardMul(battle::levelOf(s_xp)));
    ++s_escapes;
    world::markEscaped(s_battleEnemy);
    if (r.defeated && s_dropItem >= 0) {
      shop::grantDrink(s_dropItem);
      Serial.printf("[game] drop: %s drink\n", shop::name((shop::Item)s_dropItem));
    }
  }
  const battle::Outcome o = r.defeated ? battle::Outcome::Defeated   // passed it = defeated
                         : r.won ? battle::Outcome::Escaped
                         : r.gotAway ? battle::Outcome::GotAway : battle::Outcome::Caught;
  const uint8_t l0 = battle::levelOf(s_xp);
  const uint16_t gain = battle::xpGain((uint8_t)s_battleEnemy.kind, world::level(s_battleEnemy.kind), o);
  s_xp += gain;
  Serial.printf("[game] +%u XP -> %u, level %u -> %u\n", (unsigned)gain, (unsigned)s_xp, l0,
                battle::levelOf(s_xp));
  overworld::requireMoveBeforeEngage();
  Serial.printf("[game] battle %s: +%u coins, shields used %u, wallet %u\n",
                r.defeated ? "DEFEATED" : (r.won ? "ESCAPED" : (r.gotAway ? "GOT AWAY (coins lost)" : "CAUGHT (coins lost)")),
                (unsigned)r.coins, (unsigned)r.shieldsUsed,
                (unsigned)s_wallet);
  saveProfile();
  enterExplore();
}

// Music follows the mode; during runs it stops on crash / clear / game over so the
// win/lose jingles play alone. audio::music() ignores repeats of the current track.
void updateMusic() {
  using audio::Track;
  const encounter::State es = encounter::state();
  const bool running = es == encounter::State::Countdown || es == encounter::State::Run;
  switch (s_mode) {
    case Mode::Menu:
    case Mode::Settings:
    case Mode::Shop:      audio::music(Track::Title); break;
    case Mode::Explore:   audio::music(Track::Explore); break;
    case Mode::PreBattle: audio::music(Track::Battle); break;
    case Mode::Battle:    audio::music(running ? Track::Battle : Track::None); break;
    case Mode::Arcade:    audio::music(running ? Track::Arcade : Track::None); break;
  }
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
  s_lost = s_prefs.getUInt("lost", 0);
  s_gotAway = s_prefs.getUInt("gotaway", 0);
  if (s_prefs.isKey("xp")) {
    s_xp = s_prefs.getUInt("xp", 0);
  } else if (s_prefs.isKey("rankpts")) {            // L1 migration: 1 old rank point = 1 escape
    s_xp = (uint32_t)s_prefs.getUShort("rankpts", 0) * XP_ESCAPE;
    s_prefs.putUInt("xp", s_xp);
    s_prefs.remove("rankpts");
    Serial.printf("[game] migrated rank points -> %u XP\n", (unsigned)s_xp);
  }
  overworld::setEnergy(s_prefs.getFloat("energy", 0.0f));
  overworld::setWalkedM(s_prefs.getFloat("walked", 0.0f));
  locator::Pos start;
  if (s_prefs.isKey("tx")) {
    start = { s_prefs.getInt("tx", 0), s_prefs.getInt("ty", 0), OW_TILE_M * 0.5f, OW_TILE_M * 0.5f };
  } else {
    start = findLand(0, 0);
  }
  shop::load(s_prefs);
  settings::load(s_prefs);
  shop::setWalletView(&s_wallet);
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
      if (tapIn(in, BTN_EXPLORE_X, BTN_Y, BTN_W, BTN_H)) { audio::play(audio::Sfx::Tap); enterExplore(); }
      else if (tapIn(in, BTN_SHOP_X, BTN_Y, BTN_W, BTN_H)) { audio::play(audio::Sfx::Tap); enterShop(); }
      else if (tapIn(in, BTN_SET_X, BTN_SET_Y, BTN_SET_W, BTN_SET_H)) {
        audio::play(audio::Sfx::Tap);
        settings::open();
        layersSettings();
        s_mode = Mode::Settings;
      }
      else if (tapIn(in, BTN_ARCADE_X, BTN_Y, BTN_W, BTN_H)) {
        audio::play(audio::Sfx::Tap);
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
      else if (overworld::takeEngagement(e)) engage(e);
      break;
    }

    case Mode::PreBattle: {
      const int r = shop::updatePreBattle(in, dt);
      if (r == 1) enterBattle(s_battleEnemy);
      else if (r == 2) { overworld::requireMoveBeforeEngage(); enterExplore(); }
      break;
    }

    case Mode::Shop:
      if (shop::updateShop(in, s_wallet, dt)) { saveProfile(); enterMenu(); }
      break;

    case Mode::Settings:
      if (settings::update(in, dt)) { saveProfile(); enterMenu(); }
      break;

    case Mode::Battle:
      encounter::update(dt);
      if (encounter::finished()) finishBattle();
      break;

    case Mode::Arcade: {
      encounter::update(dt);
      const uint32_t banked = encounter::takeArcadeCoins();
      if (banked) {
        s_wallet += banked;
        Serial.printf("[game] arcade: +%u coins to wallet (%u)\n", (unsigned)banked, (unsigned)s_wallet);
        saveProfile();                        // once per game over
      }
      if (encounter::finished() && encounter::result().exitToMenu) enterMenu();
      break;
    }
  }
  updateMusic();
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
    case Mode::PreBattle: return "PREBATTLE";
    case Mode::Battle:  return "BATTLE";
    case Mode::Shop:    return "SHOP";
    case Mode::Settings: return "SETTINGS";
    case Mode::Arcade:  return "ARCADE";
  }
  return "?";
}

void composeMenu(lgfx::LGFX_Sprite& b, int32_t bandY) {
  using hud::text; using hud::rect; using hud::frame;
  char buf[64];
  const int32_t cx = LCD_WIDTH / 2;
  const uint16_t WHITE = rgb565(255, 255, 255), YELLOW = rgb565(255, 220, 40);

  text(b, bandY, "RUNNERS", cx, 72, hud::F24, 1.4f, YELLOW);
  rect(b, bandY, BTN_SET_X, BTN_SET_Y, BTN_SET_W, BTN_SET_H, rgb565(50, 50, 64));
  frame(b, bandY, BTN_SET_X, BTN_SET_Y, BTN_SET_W, BTN_SET_H, WHITE);
  text(b, bandY, "SETTINGS", BTN_SET_X + BTN_SET_W / 2, BTN_SET_Y + BTN_SET_H / 2, hud::F12, 1.0f, WHITE,
       BTN_SET_W - 8);

  struct Btn { int32_t x; const char* label; const char* sub; uint16_t fill; };
  const Btn btns[3] = {
    { BTN_EXPLORE_X, "EXPLORE", "map + battles", rgb565(30, 110, 60) },
    { BTN_SHOP_X,    "SHOP",    "sneakers + drinks", rgb565(40, 70, 150) },
    { BTN_ARCADE_X,  "ARCADE",  "endless run", rgb565(150, 70, 20) },
  };
  for (const Btn& k : btns) {
    rect(b, bandY, k.x, BTN_Y, BTN_W, BTN_H, k.fill);
    frame(b, bandY, k.x, BTN_Y, BTN_W, BTN_H, WHITE);
    text(b, bandY, k.label, k.x + BTN_W / 2, BTN_Y + 26, hud::F18, 1.0f, WHITE, BTN_W - 12);
    text(b, bandY, k.sub, k.x + BTN_W / 2, BTN_Y + 52, hud::F9, 1.0f, WHITE, BTN_W - 8);
  }

  snprintf(buf, sizeof buf, "LEVEL %u   Coins %u   Best %u", (unsigned)battle::levelOf(s_xp),
           (unsigned)s_wallet, (unsigned)encounter::bestScore());
  text(b, bandY, buf, cx, 222, hud::F12, 1.0f, WHITE);
  snprintf(buf, sizeof buf, "Won %u   Lost %u   Got away %u   Walked %d m", (unsigned)s_escapes,
           (unsigned)s_lost, (unsigned)s_gotAway, (int)overworld::walkedM());
  text(b, bandY, buf, cx, 252, hud::F9, 1.0f, rgb565(200, 200, 200));
}

} // namespace game_state
