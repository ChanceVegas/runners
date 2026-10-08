// shop.cpp — see shop.h. Two full-screen UI layers drawn with hud helpers (a few
// dozen rect/text calls, band-clipped). All prices, tiers and effects in config.h.
#include "shop.h"
#include "hud.h"
#include "audio.h"
#include "color.h"
#include "config.h"
#include "board_config.h"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>

namespace {

using shop::Item;

uint8_t s_owned[shop::ItemCount] = {};
bool    s_use[3] = {};                       // pre-battle: RUSH / GUARD / SURGE chosen
const uint32_t* s_walletView = nullptr;
float   s_t = 0.0f;                          // s since the screen opened (tap lockout)
char    s_msg[40] = "";
float   s_msgT = 0.0f;
uint16_t s_msgColor = 0xFFFF;
char    s_enemy[32] = "";

constexpr const char* NAMES[shop::ItemCount] = { "SPRINT", "SPRING", "GRIP", "RUSH", "GUARD", "SURGE" };
constexpr const char* DESC[shop::ItemCount]  = {
  "outrun them faster", "jump higher", "stumble less",
  "+15 m head start", "+1 shield", "orbs hit for 2" };
constexpr const char* ROMAN[4] = { "", "I", "II", "III" };
constexpr uint16_t TIER_PRICE[3] = { SHOP_TIER1_COINS, SHOP_TIER2_COINS, SHOP_TIER3_COINS };
constexpr uint16_t DRINK_PRICE[3] = { DRINK_RUSH_COINS, DRINK_GUARD_COINS, DRINK_SURGE_COINS };

// Layout (screen px).
constexpr int32_t CARD_W = 144, CARD_H = 96, CARD_X0 = 12, CARD_DX = 156;
constexpr int32_t ROW0_Y = 56, ROW1_Y = 160;
constexpr int32_t BACK_X = 8, BACK_Y = 8, BACK_W = 96, BACK_H = 38;
constexpr int32_t PB_CARD_Y = 78, PB_CARD_H = 104;
constexpr int32_t LEAVE_X = 12, LEAVE_Y = 204, LEAVE_W = 130, LEAVE_H = 50;
constexpr int32_t FIGHT_X = 300, FIGHT_Y = 198, FIGHT_W = 168, FIGHT_H = 60;

constexpr uint16_t C_BG     = rgb565(18, 20, 34);
constexpr uint16_t C_CARD   = rgb565(40, 44, 70);
constexpr uint16_t C_WHITE  = rgb565(255, 255, 255);
constexpr uint16_t C_GREY   = rgb565(130, 130, 140);
constexpr uint16_t C_YELLOW = rgb565(255, 220, 40);
constexpr uint16_t C_RED    = rgb565(235, 60, 50);
constexpr uint16_t C_GREEN  = rgb565(60, 220, 90);
constexpr uint16_t C_CYAN   = rgb565(120, 230, 255);

bool isSneaker(int i) { return i < 3; }
uint8_t maxOf(int i) { return isSneaker(i) ? shop::SNEAKER_TIERS : DRINK_MAX; }
// 0 = maxed out.
uint32_t priceOf(int i) {
  if (s_owned[i] >= maxOf(i)) return 0;
  return isSneaker(i) ? TIER_PRICE[s_owned[i]] : DRINK_PRICE[i - 3];
}

bool tapIn(const input::State& in, int32_t x, int32_t y, int32_t w, int32_t h) {
  return in.pressed && in.pointX >= x && in.pointX < x + w && in.pointY >= y && in.pointY < y + h;
}

void message(const char* m, uint16_t c) {
  strncpy(s_msg, m, sizeof s_msg - 1);
  s_msg[sizeof s_msg - 1] = 0;
  s_msgT = 1.6f;
  s_msgColor = c;
}

void walletReadout(lgfx::LGFX_Sprite& b, int32_t bandY) {
  if (!s_walletView || !hud::rowsHit(10, 44, bandY, b.height())) return;
  char buf[16];
  b.fillEllipse(LCD_WIDTH - 104, 27 - bandY, 8, 9, rgb565(240, 180, 20));
  snprintf(buf, sizeof buf, "%u", (unsigned)*s_walletView);
  hud::text(b, bandY, buf, LCD_WIDTH - 52, 27, hud::F12, 1.0f, C_YELLOW, 84);
}

void button(lgfx::LGFX_Sprite& b, int32_t bandY, int32_t x, int32_t y, int32_t w, int32_t h,
            const char* label, uint16_t fill, const lgfx::IFont* f) {
  hud::rect(b, bandY, x, y, w, h, fill);
  hud::frame(b, bandY, x, y, w, h, C_WHITE);
  hud::text(b, bandY, label, x + w / 2, y + h / 2, f, 1.0f, C_WHITE, w - 10);
}

} // namespace

namespace shop {

void load(Preferences& p) {
  char key[4] = "g0";
  for (int i = 0; i < ItemCount; ++i) {
    key[1] = (char)('0' + i);
    uint8_t v = p.getUChar(key, 0);
    s_owned[i] = v > maxOf(i) ? maxOf(i) : v;
  }
  Serial.printf("[shop] sneakers SPRINT %u SPRING %u GRIP %u | drinks RUSH %u GUARD %u SURGE %u\n",
                s_owned[0], s_owned[1], s_owned[2], s_owned[3], s_owned[4], s_owned[5]);
}

void save(Preferences& p) {
  char key[4] = "g0";
  for (int i = 0; i < ItemCount; ++i) {
    key[1] = (char)('0' + i);
    p.putUChar(key, s_owned[i]);
  }
}

uint8_t owned(Item i) { return s_owned[i]; }
const char* name(Item i) { return NAMES[i]; }
void setWalletView(const uint32_t* w) { s_walletView = w; }

// ---- SHOP ------------------------------------------------------------------------
void openShop() { s_t = 0.0f; s_msgT = 0.0f; }

bool updateShop(const input::State& in, uint32_t& wallet, float dt) {
  s_t += dt;
  if (s_msgT > 0.0f) s_msgT -= dt;
  if (s_t < 0.35f) return false;
  if (tapIn(in, BACK_X, BACK_Y, BACK_W, BACK_H)) { audio::play(audio::Sfx::Tap); return true; }
  for (int i = 0; i < ItemCount; ++i) {
    const int32_t x = CARD_X0 + (i % 3) * CARD_DX, y = (i < 3) ? ROW0_Y : ROW1_Y;
    if (!tapIn(in, x, y, CARD_W, CARD_H)) continue;
    const uint32_t price = priceOf(i);
    char buf[40];
    if (price == 0) {
      message(isSneaker(i) ? "ALREADY MAXED" : "CARRYING THE MAX", C_GREY);
      audio::play(audio::Sfx::Tap);
    } else if (wallet < price) {
      snprintf(buf, sizeof buf, "NEED %u MORE COINS", (unsigned)(price - wallet));
      message(buf, C_RED);
      audio::play(audio::Sfx::Hit);
    } else {
      wallet -= price;
      ++s_owned[i];
      if (isSneaker(i)) snprintf(buf, sizeof buf, "BOUGHT %s %s", NAMES[i], ROMAN[s_owned[i]]);
      else snprintf(buf, sizeof buf, "BOUGHT A %s DRINK", NAMES[i]);
      message(buf, C_GREEN);
      audio::play(audio::Sfx::Coin);
      Serial.printf("[shop] %s -> %u, wallet %u\n", NAMES[i], s_owned[i], (unsigned)wallet);
    }
  }
  return false;
}

void composeShop(lgfx::LGFX_Sprite& b, int32_t bandY) {
  char buf[24];
  const uint32_t wallet = s_walletView ? *s_walletView : 0;
  hud::rect(b, bandY, 0, 0, LCD_WIDTH, LCD_HEIGHT, C_BG);
  button(b, bandY, BACK_X, BACK_Y, BACK_W, BACK_H, "BACK", rgb565(50, 50, 64), hud::F12);
  if (s_msgT > 0.0f) hud::text(b, bandY, s_msg, LCD_WIDTH / 2, 27, hud::F12, 1.0f, s_msgColor, 250);
  else hud::text(b, bandY, "SHOP", LCD_WIDTH / 2, 27, hud::F18, 1.0f, C_YELLOW);
  walletReadout(b, bandY);

  for (int i = 0; i < ItemCount; ++i) {
    const int32_t x = CARD_X0 + (i % 3) * CARD_DX, y = (i < 3) ? ROW0_Y : ROW1_Y;
    if (!hud::rowsHit(y, y + CARD_H, bandY, b.height())) continue;
    const uint32_t price = priceOf(i);
    const bool afford = price && wallet >= price;
    const int32_t cx = x + CARD_W / 2;
    hud::rect(b, bandY, x, y, CARD_W, CARD_H, C_CARD);
    hud::frame(b, bandY, x, y, CARD_W, CARD_H, afford ? C_WHITE : C_GREY);
    hud::text(b, bandY, NAMES[i], cx, y + 15, hud::F12, 1.0f, isSneaker(i) ? C_WHITE : C_CYAN);
    hud::text(b, bandY, DESC[i], cx, y + 35, hud::F9, 1.0f, rgb565(200, 200, 210), CARD_W - 8);
    if (isSneaker(i)) {                                         // tier pips
      for (int t = 0; t < SNEAKER_TIERS; ++t)
        hud::rect(b, bandY, cx - 33 + t * 24, y + 50, 18, 8, t < s_owned[i] ? C_GREEN : rgb565(70, 70, 84));
    } else {
      snprintf(buf, sizeof buf, "carry %u / %u", s_owned[i], (unsigned)DRINK_MAX);
      hud::text(b, bandY, buf, cx, y + 55, hud::F9, 1.0f, C_WHITE);
    }
    if (price == 0) hud::text(b, bandY, "MAX", cx, y + 78, hud::F12, 1.0f, C_GREY);
    else {
      snprintf(buf, sizeof buf, "%u", (unsigned)price);
      hud::text(b, bandY, buf, cx, y + 78, hud::F12, 1.0f, afford ? C_YELLOW : C_RED);
    }
  }
}

// ---- PRE-BATTLE -------------------------------------------------------------------
bool hasDrinks() { return s_owned[Rush] || s_owned[Guard] || s_owned[Surge]; }

void openPreBattle(const char* enemyName, uint8_t level) {
  snprintf(s_enemy, sizeof s_enemy, "%s  LV %u", enemyName, (unsigned)level);
  s_use[0] = s_use[1] = s_use[2] = false;
  s_t = 0.0f;
}

int updatePreBattle(const input::State& in, float dt) {
  s_t += dt;
  if (s_t < 0.5f) return 0;                // the engage tap must not pick anything
  if (tapIn(in, FIGHT_X, FIGHT_Y, FIGHT_W, FIGHT_H)) { audio::play(audio::Sfx::Tap); return 1; }
  if (tapIn(in, LEAVE_X, LEAVE_Y, LEAVE_W, LEAVE_H)) { audio::play(audio::Sfx::Tap); return 2; }
  for (int d = 0; d < 3; ++d) {
    const int32_t x = CARD_X0 + d * CARD_DX;
    if (!tapIn(in, x, PB_CARD_Y, CARD_W, PB_CARD_H)) continue;
    if (s_owned[Rush + d] == 0) { audio::play(audio::Sfx::Hit); continue; }
    s_use[d] = !s_use[d];
    audio::play(s_use[d] ? audio::Sfx::Orb : audio::Sfx::Tap);
  }
  return 0;
}

void composePreBattle(lgfx::LGFX_Sprite& b, int32_t bandY) {
  char buf[24];
  hud::rect(b, bandY, 0, 0, LCD_WIDTH, LCD_HEIGHT, C_BG);
  hud::text(b, bandY, s_enemy, LCD_WIDTH / 2, 24, hud::F18, 1.0f, C_RED);
  hud::text(b, bandY, "Tap drinks to use them in this battle", LCD_WIDTH / 2, 56, hud::F9, 1.0f, C_WHITE);
  for (int d = 0; d < 3; ++d) {
    const int32_t x = CARD_X0 + d * CARD_DX, y = PB_CARD_Y, cx = x + CARD_W / 2;
    if (!hud::rowsHit(y, y + PB_CARD_H, bandY, b.height())) continue;
    const uint8_t have = s_owned[Rush + d];
    hud::rect(b, bandY, x, y, CARD_W, PB_CARD_H, s_use[d] ? rgb565(30, 90, 50) : C_CARD);
    hud::frame(b, bandY, x, y, CARD_W, PB_CARD_H, s_use[d] ? C_GREEN : (have ? C_WHITE : C_GREY));
    hud::text(b, bandY, NAMES[Rush + d], cx, y + 16, hud::F12, 1.0f, have ? C_CYAN : C_GREY);
    hud::text(b, bandY, DESC[Rush + d], cx, y + 38, hud::F9, 1.0f, rgb565(200, 200, 210), CARD_W - 8);
    snprintf(buf, sizeof buf, "x%u carried", have);
    hud::text(b, bandY, buf, cx, y + 60, hud::F9, 1.0f, have ? C_WHITE : C_GREY);
    if (s_use[d]) hud::text(b, bandY, "USING", cx, y + 84, hud::F12, 1.0f, C_GREEN);
  }
  button(b, bandY, LEAVE_X, LEAVE_Y, LEAVE_W, LEAVE_H, "LEAVE", rgb565(60, 60, 72), hud::F12);
  button(b, bandY, FIGHT_X, FIGHT_Y, FIGHT_W, FIGHT_H, "RUN!", rgb565(170, 40, 40), hud::F18);
}

battle::Stats takeBattleStats(uint8_t& guardShield) {
  static const float SPRINT[4] = { 0.0f, SNEAK_SPRINT_T1, SNEAK_SPRINT_T2, SNEAK_SPRINT_T3 };
  static const float SPRING[4] = { 1.0f, SNEAK_SPRING_T1, SNEAK_SPRING_T2, SNEAK_SPRING_T3 };
  static const float GRIP[4]   = { 1.0f, SNEAK_GRIP_T1,   SNEAK_GRIP_T2,   SNEAK_GRIP_T3 };
  battle::Stats st;
  st.gapGainBonus = SPRINT[s_owned[Sprint]];
  st.jumpMul      = SPRING[s_owned[Spring]];
  st.gapLossMul   = GRIP[s_owned[Grip]];
  st.stumbleMul   = GRIP[s_owned[Grip]];
  st.extraHearts  = s_owned[Grip] >= 3 ? 1 : 0;
  guardShield = 0;
  if (s_use[0] && s_owned[Rush])  { --s_owned[Rush];  st.startGapBonus = DRINK_RUSH_GAP_M; }
  if (s_use[1] && s_owned[Guard]) { --s_owned[Guard]; guardShield = 1; }
  if (s_use[2] && s_owned[Surge]) { --s_owned[Surge]; st.orbPower = 2; }
  s_use[0] = s_use[1] = s_use[2] = false;
  return st;
}

int8_t rollDefeatDrop() {
  if ((int)(esp_random() % 100) >= DRINK_DROP_PCT) return -1;
  int8_t open[3]; int n = 0;
  for (int d = Rush; d <= Surge; ++d) if (s_owned[d] < DRINK_MAX) open[n++] = (int8_t)d;
  return n ? open[esp_random() % n] : -1;
}

void grantDrink(int8_t item) {
  if (item >= Rush && item <= Surge && s_owned[item] < DRINK_MAX) ++s_owned[item];
}

}
