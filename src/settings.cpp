// settings.cpp — see settings.h.
#include "settings.h"
#include "audio.h"
#include "hud.h"
#include "color.h"
#include "config.h"
#include "board_config.h"
#include <Arduino.h>
#include <stdio.h>

namespace {

float s_t = 0.0f;

// Layout: two rows of [-] [10-segment bar] [+], a MUTE toggle, BACK top-left.
constexpr int32_t ROW_Y[2] = { 70, 140 };
constexpr int32_t ROW_H = 54;
constexpr int32_t MINUS_X = 120, BTN_W = 60, BAR_X = 192, BAR_W = 200, PLUS_X = 404;
constexpr int32_t MUTE_X = 150, MUTE_Y = 210, MUTE_W = 180, MUTE_H = 48;
constexpr int32_t BACK_X = 8, BACK_Y = 8, BACK_W = 96, BACK_H = 38;

constexpr uint16_t C_BG    = rgb565(18, 20, 34);
constexpr uint16_t C_WHITE = rgb565(255, 255, 255);
constexpr uint16_t C_DIM   = rgb565(70, 70, 84);
constexpr uint16_t C_ON    = rgb565(60, 220, 90);
constexpr uint16_t C_BTN   = rgb565(50, 50, 64);

bool tapIn(const input::State& in, int32_t x, int32_t y, int32_t w, int32_t h) {
  return in.pressed && in.pointX >= x && in.pointX < x + w && in.pointY >= y && in.pointY < y + h;
}

void button(lgfx::LGFX_Sprite& b, int32_t bandY, int32_t x, int32_t y, int32_t w, int32_t h,
            const char* label, uint16_t fill) {
  hud::rect(b, bandY, x, y, w, h, fill);
  hud::frame(b, bandY, x, y, w, h, C_WHITE);
  hud::text(b, bandY, label, x + w / 2, y + h / 2, hud::F18, 1.0f, C_WHITE, w - 8);
}

} // namespace

namespace settings {

void load(Preferences& p) {
  audio::setLevels(p.getUChar("vmus", AUDIO_DEFAULT_LEVEL), p.getUChar("vsfx", AUDIO_DEFAULT_LEVEL));
  audio::setMuted(p.getBool("mute", false));
}

void save(Preferences& p) {
  p.putUChar("vmus", audio::musicLevel());
  p.putUChar("vsfx", audio::sfxLevel());
  p.putBool("mute", audio::muted());
}

void open() { s_t = 0.0f; }

bool update(const input::State& in, float dt) {
  s_t += dt;
  if (s_t < 0.35f) return false;
  if (tapIn(in, BACK_X, BACK_Y, BACK_W, BACK_H)) { audio::play(audio::Sfx::Tap); return true; }
  uint8_t m = audio::musicLevel(), s = audio::sfxLevel();
  for (int r = 0; r < 2; ++r) {
    uint8_t& v = r == 0 ? m : s;
    if (tapIn(in, MINUS_X, ROW_Y[r], BTN_W, ROW_H) && v > 0) --v;
    else if (tapIn(in, PLUS_X, ROW_Y[r], BTN_W, ROW_H) && v < AUDIO_LEVELS) ++v;
    else continue;
    audio::setLevels(m, s);
    if (audio::muted()) audio::setMuted(false);     // changing a level un-mutes
    audio::play(r == 0 ? audio::Sfx::Tap : audio::Sfx::Coin);   // preview the new level
  }
  if (tapIn(in, MUTE_X, MUTE_Y, MUTE_W, MUTE_H)) {
    audio::setMuted(!audio::muted());
    audio::play(audio::Sfx::Tap);
  }
  return false;
}

void compose(lgfx::LGFX_Sprite& b, int32_t bandY) {
  hud::rect(b, bandY, 0, 0, LCD_WIDTH, LCD_HEIGHT, C_BG);
  button(b, bandY, BACK_X, BACK_Y, BACK_W, BACK_H, "BACK", C_BTN);
  hud::text(b, bandY, "SETTINGS", LCD_WIDTH / 2, 27, hud::F18, 1.0f, rgb565(255, 220, 40));
  const char* names[2] = { "MUSIC", "SOUND" };
  const uint8_t lv[2] = { audio::musicLevel(), audio::sfxLevel() };
  for (int r = 0; r < 2; ++r) {
    const int32_t y = ROW_Y[r];
    if (!hud::rowsHit(y, y + ROW_H, bandY, b.height())) continue;
    hud::text(b, bandY, names[r], 60, y + ROW_H / 2, hud::F12, 1.0f, C_WHITE, 100);
    button(b, bandY, MINUS_X, y, BTN_W, ROW_H, "-", C_BTN);
    button(b, bandY, PLUS_X, y, BTN_W, ROW_H, "+", C_BTN);
    const int32_t seg = BAR_W / AUDIO_LEVELS;
    for (int i = 0; i < AUDIO_LEVELS; ++i)       // bars grow taller to the right
      hud::rect(b, bandY, BAR_X + i * seg + 2, y + ROW_H - 10 - i * 3, seg - 4, 10 + i * 3,
                i < lv[r] ? (audio::muted() ? rgb565(110, 110, 120) : C_ON) : C_DIM);
  }
  button(b, bandY, MUTE_X, MUTE_Y, MUTE_W, MUTE_H, audio::muted() ? "UNMUTE" : "MUTE ALL",
         audio::muted() ? rgb565(170, 40, 40) : C_BTN);
}

void drawMuteIcon(lgfx::LGFX_Sprite& b, int32_t bandY) {
  const int32_t x = LCD_WIDTH - INPUT_UI_CORNER_W + 12, y = 12;
  if (!hud::rowsHit(y - 2, y + 26, bandY, b.height())) return;
  const uint16_t c = audio::muted() ? rgb565(235, 60, 50) : rgb565(220, 220, 230);
  hud::rect(b, bandY, x, y + 8, 8, 10, c);                       // speaker body
  b.fillTriangle(x + 6, y + 13 - bandY, x + 18, y + 1 - bandY, x + 18, y + 25 - bandY, c);
  if (audio::muted()) {                                          // slash
    b.drawLine(x - 2, y + 26 - bandY, x + 28, y - 2 - bandY, c);
    b.drawLine(x - 1, y + 26 - bandY, x + 29, y - 2 - bandY, c);
  } else {                                                       // sound waves
    hud::rect(b, bandY, x + 22, y + 9, 3, 8, c);
    hud::rect(b, bandY, x + 28, y + 5, 3, 16, c);
  }
}

bool muteCornerTapped(const input::State& in) {
  return in.pressed && in.pointY >= 0 && in.pointY < INPUT_UI_CORNER_H &&
         in.pointX >= LCD_WIDTH - INPUT_UI_CORNER_W;
}

}
