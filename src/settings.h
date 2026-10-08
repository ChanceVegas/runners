// settings.h — SETTINGS screen (menu button): Music and Sound volume levels and mute.
// Owns loading/saving those values (NVS "profile": vmus, vsfx, mute) and pushes them
// to audio. The in-run mute corner (encounter) changes audio::muted() directly; save()
// persists whatever audio currently has.
#pragma once
#include <LovyanGFX.hpp>
#include <Preferences.h>
#include "input.h"

namespace settings {

void load(Preferences& p);           // read levels + mute and apply them to audio
void save(Preferences& p);           // write audio's current levels + mute

void open();
bool update(const input::State& in, float dt);   // true when BACK is tapped
void compose(lgfx::LGFX_Sprite& band, int32_t bandY);

// Small speaker icon (with a slash when muted) in the top-right UI corner; used by the
// in-run HUD. Tapping that corner never moves the runner (input reserves it).
void drawMuteIcon(lgfx::LGFX_Sprite& band, int32_t bandY);
bool muteCornerTapped(const input::State& in);

}
