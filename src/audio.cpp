// audio.cpp — see audio.h. SFX voices + M1 music sequencer, mixed in one task. Legacy ESP-IDF 4.4 I2S driver (Arduino core 2.0.14),
// 16-bit stereo at AUDIO_SAMPLE_HZ. The synth task (core 0, below the push task's
// work) renders 128-frame blocks; i2s_write blocks on DMA space, which paces it.
// Each SFX is a short list of steps {Hz, ms, wave}; Hz 0 = rest. Two voices so a coin
// can ring over a jump. All state is static; no allocation after init.
#include "audio.h"
#include "config.h"
#include "board_config.h"
#include <Arduino.h>
#include <driver/i2s.h>
#include <math.h>
#include "music_data.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {

enum Wave : uint8_t { SQ, NOISE };
struct Step { uint16_t hz; uint16_t ms; Wave wave; int16_t slideHz; };  // slide: Hz change over the step

#define S(hz, ms) { hz, ms, SQ, 0 }
const Step BOOT[] = { S(523, 90), S(659, 90), S(784, 160), {0, 0, SQ, 0} };
const Step TAP[]  = { S(880, 40), {0, 0, SQ, 0} };
const Step JUMP[] = { { 400, 110, SQ, 500 }, {0, 0, SQ, 0} };
const Step DUCK[] = { { 500, 90, SQ, -250 }, {0, 0, SQ, 0} };
const Step COIN[] = { S(988, 50), S(1319, 110), {0, 0, SQ, 0} };
const Step ORB[]  = { { 600, 160, SQ, 900 }, S(1568, 80), {0, 0, SQ, 0} };
const Step HIT[]  = { { 900, 220, NOISE, -700 }, { 110, 120, SQ, -40 }, {0, 0, SQ, 0} };
const Step WIN[]  = { S(523, 100), S(659, 100), S(784, 100), S(1047, 260), {0, 0, SQ, 0} };
const Step LOSE[] = { S(392, 160), S(330, 160), S(262, 320), {0, 0, SQ, 0} };
#undef S
const Step* const TABLE[(int)audio::Sfx::Count] = { BOOT, TAP, JUMP, DUCK, COIN, ORB, HIT, WIN, LOSE };

struct Voice {
  const Step* step = nullptr;   // current step, nullptr = idle
  uint32_t left = 0;            // samples left in this step
  uint32_t total = 0;
  float phase = 0.0f;           // 0..1
  uint32_t lfsr = 0xACE1u;
};
Voice s_v[2];

portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
const Step* s_req[2] = { nullptr, nullptr };   // pending starts (set by play, taken by task)
uint8_t s_next = 0;
bool s_ok = false;

constexpr int FRAMES = 128;
int16_t s_buf[FRAMES * 2];
const int16_t AMP = (int16_t)(32767 * AUDIO_VOLUME_PCT / 100 / 2);   // /2: two voices summed

void startStep(Voice& v) {
  if (!v.step || v.step->ms == 0) { v.step = nullptr; return; }
  v.total = v.left = (uint32_t)v.step->ms * AUDIO_SAMPLE_HZ / 1000;
}

int16_t sample(Voice& v) {
  if (!v.step) return 0;
  if (v.left == 0) { ++v.step; startStep(v); if (!v.step) return 0; }
  const Step& st = *v.step;
  --v.left;
  if (st.hz == 0) return 0;
  const float t = 1.0f - (float)v.left / (float)v.total;           // 0..1 through the step
  const float hz = st.hz + st.slideHz * t;
  if (st.wave == NOISE) {
    v.phase += hz / AUDIO_SAMPLE_HZ;
    if (v.phase >= 1.0f) {                                         // clock the LFSR at hz
      v.phase -= 1.0f;
      v.lfsr = (v.lfsr >> 1) ^ (-(int32_t)(v.lfsr & 1u) & 0xB400u);
    }
    return (v.lfsr & 1u) ? AMP : -AMP;
  }
  v.phase += hz / AUDIO_SAMPLE_HZ;
  if (v.phase >= 1.0f) v.phase -= 1.0f;
  // Short fade-out over the last 3 ms of a step to avoid clicks between notes.
  const uint32_t fade = AUDIO_SAMPLE_HZ * 3 / 1000;
  const int32_t a = (v.left < fade) ? (int32_t)AMP * (int32_t)v.left / (int32_t)fade : AMP;
  return (int16_t)(v.phase < 0.5f ? a : -a);
}

// ---- Music (M1): row sequencer over SONGS (music_data.h) -------------------------
// Three music channels under the SFX voices: lead (square, song duty, decaying),
// bass (50% square, octave 3 so a tiny speaker can play it), drums (kick = falling
// square, snare/hat = noise bursts). Quieter than SFX (MUSIC_VOLUME_PCT).
constexpr float MAMP = 32767.0f * MUSIC_VOLUME_PCT / 100.0f;
float s_midiHz[128];

struct Music {
  const SongDef* song = nullptr;   // nullptr = silent
  uint16_t row = 0;
  uint32_t rowLeft = 0, rowLen = 0;
  float leadHz = 0, leadPh = 0, leadEnv = 0;
  float bassHz = 0, bassPh = 0, bassEnv = 0;
  uint8_t drum = 0; uint32_t drumLeft = 0, drumLen = 1;
  float kickPh = 0;
  uint32_t lfsr = 0x7A3Bu; float noisePh = 0; int8_t noiseBit = 1;
};
Music s_m;
int8_t s_musicReq = -2;            // -2 none pending, -1 stop, >= 0 track

void musicStart(int8_t t) {
  s_m = Music();
  if (t < 0 || t >= (int8_t)(sizeof SONGS / sizeof SONGS[0])) return;
  s_m.song = &SONGS[t];
  s_m.rowLen = (uint32_t)(AUDIO_SAMPLE_HZ * 60.0f / (s_m.song->bpm * 4.0f));
  s_m.rowLeft = 0;                 // first sample loads row 0
  s_m.row = (uint16_t)-1;
}

void musicRow() {
  s_m.row = (uint16_t)((s_m.row + 1) % s_m.song->count);
  const uint8_t* r = s_m.song->rows[s_m.row];
  if (r[0] == 1) s_m.leadEnv = 0;
  else if (r[0] >= 2) { s_m.leadHz = s_midiHz[r[0]]; s_m.leadEnv = 1.0f; }
  if (r[1] == 1) s_m.bassEnv = 0;
  else if (r[1] >= 2) { s_m.bassHz = s_midiHz[r[1]]; s_m.bassEnv = 1.0f; }
  if (r[2]) {
    s_m.drum = r[2];
    s_m.drumLen = s_m.drumLeft = AUDIO_SAMPLE_HZ * (r[2] == 1 ? 70 : r[2] == 2 ? 110 : 25) / 1000;
    s_m.kickPh = 0;
  }
  s_m.rowLeft = s_m.rowLen;
}

inline float squareWave(float& ph, float hz, float duty) {
  ph += hz * (1.0f / AUDIO_SAMPLE_HZ);
  if (ph >= 1.0f) ph -= 1.0f;
  return ph < duty ? 1.0f : -1.0f;
}

int32_t musicSample() {
  if (!s_m.song) return 0;
  if (s_m.rowLeft == 0) musicRow();
  --s_m.rowLeft;
  float out = 0.0f;
  if (s_m.leadEnv > 0.01f) {                    // lead: decays toward a 45% sustain
    out += 0.42f * s_m.leadEnv * squareWave(s_m.leadPh, s_m.leadHz, 1.0f / s_m.song->duty);
    if (s_m.leadEnv > 0.45f) s_m.leadEnv *= 0.99985f;
  }
  if (s_m.bassEnv > 0.01f) {
    out += 0.34f * s_m.bassEnv * squareWave(s_m.bassPh, s_m.bassHz, 0.5f);
    if (s_m.bassEnv > 0.6f) s_m.bassEnv *= 0.99990f;
  }
  if (s_m.drumLeft) {
    const float k = (float)s_m.drumLeft / (float)s_m.drumLen;     // 1 -> 0
    --s_m.drumLeft;
    if (s_m.drum == 1) {                                          // kick: 160 -> 50 Hz
      out += 0.45f * k * squareWave(s_m.kickPh, 50.0f + 110.0f * k, 0.5f);
    } else {                                                      // snare / hat: noise
      s_m.noisePh += (s_m.drum == 2 ? 6000.0f : 14000.0f) * (1.0f / AUDIO_SAMPLE_HZ);
      if (s_m.noisePh >= 1.0f) {
        s_m.noisePh -= 1.0f;
        s_m.lfsr = (s_m.lfsr >> 1) ^ (-(int32_t)(s_m.lfsr & 1u) & 0xB400u);
        s_m.noiseBit = (s_m.lfsr & 1u) ? 1 : -1;
      }
      out += (s_m.drum == 2 ? 0.30f : 0.16f) * k * s_m.noiseBit;
    }
  }
  return (int32_t)(out * MAMP);
}

void synthTask(void*) {
  for (;;) {
    portENTER_CRITICAL(&s_mux);
    for (int i = 0; i < 2; ++i)
      if (s_req[i]) { s_v[i].step = s_req[i]; s_v[i].phase = 0; s_req[i] = nullptr; startStep(s_v[i]); }
    const int8_t mreq = s_musicReq;
    s_musicReq = -2;
    portEXIT_CRITICAL(&s_mux);
    if (mreq != -2) musicStart(mreq);
    for (int f = 0; f < FRAMES; ++f) {
      int32_t m = (int32_t)sample(s_v[0]) + sample(s_v[1]) + musicSample();
      if (m > 32767) m = 32767;
      if (m < -32768) m = -32768;
      s_buf[f * 2] = (int16_t)m;
      s_buf[f * 2 + 1] = (int16_t)m;
    }
    size_t written = 0;
    i2s_write(I2S_NUM_0, s_buf, sizeof s_buf, &written, portMAX_DELAY);
  }
}

} // namespace

namespace audio {

bool init() {
#if !AUDIO_ENABLED
  Serial.println("[audio] disabled (AUDIO_ENABLED 0)");
  return true;
#else
  for (int n = 0; n < 128; ++n) s_midiHz[n] = 440.0f * powf(2.0f, (n - 69) / 12.0f);
  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate = AUDIO_SAMPLE_HZ;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags = 0;
  cfg.dma_buf_count = 4;
  cfg.dma_buf_len = FRAMES;
  cfg.use_apll = false;
  cfg.tx_desc_auto_clear = true;
  if (i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr) != ESP_OK) {
    Serial.println("[audio] i2s_driver_install FAILED");
    return false;
  }
  i2s_pin_config_t pins = {};
  pins.mck_io_num = I2S_PIN_NO_CHANGE;
  pins.bck_io_num = I2S_PIN_BCLK;
  pins.ws_io_num = I2S_PIN_LRCK;
  pins.data_out_num = I2S_PIN_DOUT;
  pins.data_in_num = I2S_PIN_NO_CHANGE;
  if (i2s_set_pin(I2S_NUM_0, &pins) != ESP_OK) {
    Serial.println("[audio] i2s_set_pin FAILED");
    return false;
  }
  i2s_zero_dma_buffer(I2S_NUM_0);
  // Core 0 with the push task, one priority above it (4 vs 3) so a long push never
  // starves the DMA (an underrun = a click). Rendering a block is
  // ~128 x 2 cheap samples every 8 ms — negligible next to the frame push.
  if (xTaskCreatePinnedToCore(synthTask, "audio", 3072, nullptr, 4, nullptr, 0) != pdPASS) {
    Serial.println("[audio] task create FAILED");
    return false;
  }
  s_ok = true;
  Serial.printf("[audio] I2S ok: %d Hz, BCLK %d LRCK %d DOUT %d, volume %d%%\n", AUDIO_SAMPLE_HZ,
                I2S_PIN_BCLK, I2S_PIN_LRCK, I2S_PIN_DOUT, AUDIO_VOLUME_PCT);
  return true;
#endif
}

void play(Sfx s) {
  if (!s_ok || s >= Sfx::Count) return;
  portENTER_CRITICAL(&s_mux);
  // Prefer an idle voice; otherwise alternate so the newest sound always plays.
  int v = (!s_v[0].step && !s_req[0]) ? 0 : (!s_v[1].step && !s_req[1]) ? 1 : s_next;
  s_next ^= 1;
  s_req[v] = TABLE[(int)s];
  portEXIT_CRITICAL(&s_mux);
}

bool ok() { return s_ok; }

void music(Track t) {
  static Track s_cur = Track::None;
  if (!s_ok || t == s_cur) return;              // same track keeps playing (no restart)
  s_cur = t;
  portENTER_CRITICAL(&s_mux);
  s_musicReq = (t == Track::None) ? -1 : (int8_t)t;
  portEXIT_CRITICAL(&s_mux);
}

}
