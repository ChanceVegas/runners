// audio.h — sound effects on the onboard I2S amp: a tiny 2-voice square/noise synth
// running in its own task. Game code calls play(Sfx); nothing else touches audio.
#pragma once
#include <stdint.h>

namespace audio {

enum class Sfx : uint8_t {
  Boot,      // rising 3-note chime (bring-up check)
  Tap,       // menu button
  Jump, Duck, Coin, Orb,
  Hit,       // stumble / crash
  Win, Lose,
  Count
};

bool init();          // start I2S + synth task; returns false if the driver failed
void play(Sfx s);     // fire-and-forget; a new SFX on a busy voice replaces it
bool ok();            // driver started

}
