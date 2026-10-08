// gps.cpp — see gps.h. No allocation: one fixed line buffer, fields parsed in place.
// Baud detection: listen GPS_DETECT_MS at each candidate rate; the first rate that
// yields a checksum-valid NMEA sentence wins. If the link is lost for GPS_LOST_MS the
// search restarts. Note: NMEA lat/lon need double precision (float = ~2 m at best).
#include "gps.h"
#include "config.h"
#include "board_config.h"
#include <Arduino.h>
#include <HardwareSerial.h>
#include <stdlib.h>
#include <string.h>

namespace {

HardwareSerial& U = Serial1;
const uint32_t BAUDS[] = { 9600, 38400, 115200, 57600, 19200, 4800 };
constexpr int NBAUDS = sizeof BAUDS / sizeof BAUDS[0];

gps::Fix s_fix = {};
int      s_baudIdx = -1;           // -1 = not started
uint32_t s_tryStart = 0;
bool     s_locked = false;
char     s_line[100];
int      s_len = 0;
// GSV accumulators for the current epoch (published + reset on each RMC, which
// u-blox sends once per epoch). Talker-agnostic: works for GP/GL/GA/GB/GQ sets.
uint8_t  s_gsvView = 0, s_gsvHeard = 0, s_gsvSnrMax = 0;
bool     s_echo = GPS_ECHO_DEFAULT;
uint32_t s_lastEchoMs = 0;
bool     s_echoing = false;        // echoing the current epoch (RMC to next RMC)
uint32_t s_lastByteMs = 0;         // G0-R6: silence detection (module restart evidence)
uint32_t s_prevLockBaud = 0;       // baud of the previous lock (revert = module restarted)
uint16_t s_silences = 0;
uint32_t s_lastStatusMs = 0;
uint32_t s_bytes = 0;              // raw bytes received (wiring check: 0 = nothing on RX)
uint32_t s_winSent = 0, s_winBytes = 0;   // rate window (since the last report)
uint8_t  s_winTypes = 0;
char     s_types[48] = "";

void tryBaud(int i);   // below

// ---- UBX binary parser (G0-R5) ----------------------------------------------------
// Frame: B5 62 class id lenLo lenHi payload[len] ckA ckB (8-bit Fletcher over class..
// payload). Runs on every byte alongside the NMEA line parser.
enum UbxState : uint8_t { U_SYNC1, U_SYNC2, U_CLASS, U_ID, U_LEN1, U_LEN2, U_PAYLOAD, U_CKA, U_CKB };
UbxState u_state = U_SYNC1;
uint8_t  u_cls = 0, u_id = 0, u_ckA = 0, u_ckB = 0, u_rxA = 0;
uint16_t u_len = 0, u_pos = 0;
uint8_t  u_buf[100];                 // NAV-PVT is 92 bytes; longer payloads are checksummed but not kept
uint32_t s_winUbx = 0, s_winPvt = 0;
struct UbxSeen { uint8_t cls, id; uint16_t n; };
UbxSeen  s_ubxSeen[6];               // distinct class/id pairs in the window
int      s_nUbxSeen = 0;
char     s_ubxText[64] = "";
bool     s_ubxLogged = false;

inline uint32_t u4(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
inline int32_t  i4(const uint8_t* p) { return (int32_t)u4(p); }

void ubxFrame() {
  ++s_winUbx;
  s_fix.lastMs = millis();                       // UBX counts as a live link too
  if (!s_locked) {
    s_locked = true;
    s_fix.baud = BAUDS[s_baudIdx];
    Serial.printf("[gps] LINK OK at %u baud (UBX)\n", (unsigned)s_fix.baud);
  }
  s_fix.link = true;
  int i = 0;
  for (; i < s_nUbxSeen; ++i) if (s_ubxSeen[i].cls == u_cls && s_ubxSeen[i].id == u_id) break;
  if (i == s_nUbxSeen && s_nUbxSeen < 6) s_ubxSeen[s_nUbxSeen++] = { u_cls, u_id, 0 };
  if (i < 6) ++s_ubxSeen[i].n;
  if (!s_ubxLogged) {
    s_ubxLogged = true;
    Serial.printf("[gps] UBX binary frames detected (first %02X-%02X, %u bytes)\n", u_cls, u_id, u_len);
  }
  if (u_cls == 0x01 && u_id == 0x07 && u_len == 92) {   // UBX-NAV-PVT
    ++s_winPvt;
    const uint8_t* p = u_buf;
    const uint8_t fixType = p[20], flags = p[21];
    s_fix.fixType = fixType;
    s_fix.quality = (fixType >= 2 && (flags & 0x01)) ? 1 : 0;
    s_fix.valid = s_fix.quality != 0;
    s_fix.satsUsed = p[23];
    if (p[11] & 0x02) { s_fix.hh = p[8]; s_fix.mm = p[9]; s_fix.ss = p[10]; }   // validTime
    if (s_fix.quality) {
      s_fix.lon = i4(p + 24) * 1e-7;
      s_fix.lat = i4(p + 28) * 1e-7;
      s_fix.altM = i4(p + 36) * 0.001f;
      s_fix.speedMS = i4(p + 60) * 0.001f;
      s_fix.courseDeg = i4(p + 64) * 1e-5f;
    }
    s_fix.hAccM = u4(p + 40) * 0.001f;
    s_fix.hdop = (p[76] | (p[77] << 8)) * 0.01f;        // pDOP (closest NAV-PVT has)
  }
}

// ---- Baud upgrade (G0-R5) ----------------------------------------------------------
// The module streams more than 9600 baud can carry (G0-R2 log: ~4 NMEA sentences/s with
// 10 s gaps full of non-NMEA bytes). Once linked below 115200 we ask it, in RAM only
// (a power cycle restores its own setting), to switch UART1 to 115200: both the M10
// way (CFG-VALSET CFG-UART1-BAUDRATE) and the legacy way (CFG-PRT). A module that
// doesn't know one of them just NAKs it. Then we follow to 115200; if nothing valid
// arrives there, the normal baud search finds it again and we don't retry.
bool s_upgradeTried = false;

void ubxSend(uint8_t cls, uint8_t id, const uint8_t* pl, uint16_t len) {
  uint8_t a = 0, b = 0;
  const uint8_t hdr[4] = { cls, id, (uint8_t)len, (uint8_t)(len >> 8) };
  for (uint8_t v : hdr) { a += v; b += a; }
  for (uint16_t i = 0; i < len; ++i) { a += pl[i]; b += a; }
  U.write(0xB5); U.write(0x62); U.write(hdr, 4); U.write(pl, len); U.write(a); U.write(b);
}

void requestBaud115200() {
  const uint32_t baud = 115200;
  // CFG-VALSET: version 0, layer RAM (1), 2 reserved, key 0x40520001 (U4), value.
  const uint8_t valset[12] = { 0x00, 0x01, 0x00, 0x00, 0x01, 0x00, 0x52, 0x40,
                               (uint8_t)baud, (uint8_t)(baud >> 8), (uint8_t)(baud >> 16), (uint8_t)(baud >> 24) };
  ubxSend(0x06, 0x8A, valset, sizeof valset);
  // CFG-PRT (legacy): port 1 (UART1), 8N1, baud, in UBX+NMEA, out UBX+NMEA.
  const uint8_t prt[20] = { 0x01, 0x00, 0x00, 0x00, 0xD0, 0x08, 0x00, 0x00,
                            (uint8_t)baud, (uint8_t)(baud >> 8), (uint8_t)(baud >> 16), (uint8_t)(baud >> 24),
                            0x03, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00 };
  ubxSend(0x06, 0x00, prt, sizeof prt);
  U.flush();                                     // wait until it's on the wire
  delay(60);                                     // module applies it after the frame
  Serial.printf("[gps] asked the module for 115200 baud (RAM only); following\n");
  for (int i = 0; i < NBAUDS; ++i) if (BAUDS[i] == 115200) { s_locked = false; s_fix.link = false; tryBaud(i); }
}

void ubxByte(uint8_t c) {
  switch (u_state) {
    case U_SYNC1: if (c == 0xB5) u_state = U_SYNC2; break;
    case U_SYNC2: u_state = (c == 0x62) ? U_CLASS : (c == 0xB5 ? U_SYNC2 : U_SYNC1); break;
    case U_CLASS: u_cls = c; u_ckA = c; u_ckB = c; u_state = U_ID; break;
    case U_ID:    u_id = c; u_ckA += c; u_ckB += u_ckA; u_state = U_LEN1; break;
    case U_LEN1:  u_len = c; u_ckA += c; u_ckB += u_ckA; u_state = U_LEN2; break;
    case U_LEN2:
      u_len |= (uint16_t)c << 8; u_ckA += c; u_ckB += u_ckA; u_pos = 0;
      u_state = u_len > 1024 ? U_SYNC1 : (u_len ? U_PAYLOAD : U_CKA);
      break;
    case U_PAYLOAD:
      if (u_pos < sizeof u_buf) u_buf[u_pos] = c;
      ++u_pos; u_ckA += c; u_ckB += u_ckA;
      if (u_pos >= u_len) u_state = U_CKA;
      break;
    case U_CKA: u_rxA = c; u_state = U_CKB; break;
    case U_CKB:
      if (u_rxA == u_ckA && c == u_ckB) ubxFrame();
      u_state = U_SYNC1;
      break;
  }
}
int8_t   s_rxPin = GPS_PIN_RX, s_txPin = GPS_PIN_TX;   // swapped at boot if the probe says so

// Boot-time pin probe (G0-R2): with both UART pins as pulled-down inputs, count level
// changes for GPS_PROBE_MS. The GPS TX line idles HIGH and toggles while it sends; an
// unconnected / unpowered line sits LOW. Tells "nothing wired" from "TX/RX swapped".
struct Probe { uint32_t edges; uint32_t highPct; };
Probe probePin(int pin, uint32_t ms) {
  pinMode(pin, INPUT_PULLDOWN);
  uint32_t edges = 0, high = 0, n = 0;
  int last = digitalRead(pin);
  const uint32_t t0 = millis();
  while (millis() - t0 < ms) {
    const int v = digitalRead(pin);
    if (v != last) { ++edges; last = v; }
    high += v; ++n;
  }
  return { edges, n ? high * 100 / n : 0 };
}
char     s_status[32] = "GPS starting";

void tryBaud(int i) {
  s_baudIdx = i;
  U.end();
  U.setRxBufferSize(1024);
  U.begin(BAUDS[i], SERIAL_8N1, s_rxPin, s_txPin);
  s_tryStart = millis();
  s_len = 0;
  Serial.printf("[gps] listening at %u baud\n", (unsigned)BAUDS[i]);
}

int hexv(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

// Split s_line (no '$', checksum stripped) into fields in place. Returns count.
int split(char* s, char** f, int maxF) {
  int n = 0;
  f[n++] = s;
  for (char* p = s; *p && n < maxF; ++p)
    if (*p == ',') { *p = 0; f[n++] = p + 1; }
  return n;
}

double nmeaDeg(const char* v, const char* hemi) {   // ddmm.mmmm -> degrees
  if (!*v) return 0.0;
  const double x = atof(v);
  const int deg = (int)(x / 100.0);
  double d = deg + (x - deg * 100.0) / 60.0;
  if (*hemi == 'S' || *hemi == 'W') d = -d;
  return d;
}

void handleSentence() {
  // Verify "*HH" checksum.
  char* star = strchr(s_line, '*');
  if (!star || star - s_line < 6) return;
  uint8_t sum = 0;
  for (char* p = s_line; p < star; ++p) sum ^= (uint8_t)*p;
  const int h = hexv(star[1]), l = hexv(star[2]);
  if (h < 0 || l < 0 || sum != (uint8_t)(h * 16 + l)) { ++s_fix.badSum; return; }
  *star = 0;
  ++s_fix.sentences;
  ++s_winSent;
  s_fix.lastMs = millis();
  if (!s_locked) {
    s_locked = true;
    s_fix.baud = BAUDS[s_baudIdx];
    Serial.printf("[gps] LINK OK at %u baud (first sentence: $%.5s)\n", (unsigned)s_fix.baud, s_line);
    if (s_prevLockBaud > s_fix.baud)
      Serial.printf("[gps] module came back at %u after running at %u: IT RESTARTED (RAM settings "
                    "lost) - likely a power brown-out\n", (unsigned)s_fix.baud, (unsigned)s_prevLockBaud);
    s_prevLockBaud = s_fix.baud;
  }
  s_fix.link = true;

  // G0 diagnostics: echo one whole epoch of raw sentences every GPS_ECHO_MS. An epoch
  // runs from one RMC to the next.
  if (s_line[2] == 'R' && s_line[3] == 'M' && s_line[4] == 'C') {
    if (s_echoing) { s_echoing = false; s_lastEchoMs = millis(); }
    else if (s_echo && millis() - s_lastEchoMs >= GPS_ECHO_MS) s_echoing = true;
  }
  if (strstr(s_line, "TXT"))                      // module text (boot banner, antenna status)
    Serial.printf("[gps-txt] $%s\n", s_line);
  else if (s_echoing && (strstr(s_line, "GSV") || strstr(s_line, "GGA")))
    Serial.printf("[nmea] $%s\n", s_line);

  char* f[24];
  const int n = split(s_line, f, 24);
  const char* type = f[0] + 2;                 // skip talker ("GP", "GN", "GL", ...)
  s_winTypes |= !strcmp(type, "RMC") ? 1 : !strcmp(type, "GGA") ? 2 : !strcmp(type, "GSV") ? 4
              : !strcmp(type, "GSA") ? 8 : !strcmp(type, "VTG") ? 16 : !strcmp(type, "GLL") ? 32
              : !strcmp(type, "TXT") ? 64 : 128;
  if (!strcmp(type, "GGA") && n >= 10) {
    s_fix.quality = (uint8_t)atoi(f[6]);
    s_fix.satsUsed = (uint8_t)atoi(f[7]);
    s_fix.hdop = (float)atof(f[8]);
    s_fix.altM = (float)atof(f[9]);
    if (s_fix.quality) { s_fix.lat = nmeaDeg(f[2], f[3]); s_fix.lon = nmeaDeg(f[4], f[5]); }
  } else if (!strcmp(type, "RMC") && n >= 9) {
    s_fix.satsView = s_gsvView; s_fix.satsHeard = s_gsvHeard; s_fix.snrMax = s_gsvSnrMax;
    s_gsvView = s_gsvHeard = s_gsvSnrMax = 0;
    s_fix.valid = (f[2][0] == 'A');
    if (strlen(f[1]) >= 6) {
      s_fix.hh = (uint8_t)((f[1][0] - '0') * 10 + f[1][1] - '0');
      s_fix.mm = (uint8_t)((f[1][2] - '0') * 10 + f[1][3] - '0');
      s_fix.ss = (uint8_t)((f[1][4] - '0') * 10 + f[1][5] - '0');
    }
    if (s_fix.valid) {
      s_fix.lat = nmeaDeg(f[3], f[4]);
      s_fix.lon = nmeaDeg(f[5], f[6]);
      s_fix.speedMS = (float)(atof(f[7]) * 0.514444);   // knots -> m/s
      s_fix.courseDeg = (float)atof(f[8]);
    }
  } else if (!strcmp(type, "GSV") && n >= 4) {
    // $xxGSV,total,msgNum,inView,{prn,elev,az,snr}x(<=4)[,signalId]
    if (atoi(f[2]) == 1) s_gsvView += (uint8_t)atoi(f[3]);
    for (int i = 4; i + 3 < n; i += 4) {
      const int snr = f[i + 3][0] ? atoi(f[i + 3]) : 0;
      if (snr > 0) { ++s_gsvHeard; if (snr > s_gsvSnrMax) s_gsvSnrMax = (uint8_t)snr; }
    }
  }
}

} // namespace

namespace gps {

bool init() {
  // Probe both pins at once (sequentially, GPS_PROBE_MS each; ~2.4 s at boot).
  const Probe pr = probePin(GPS_PIN_RX, GPS_PROBE_MS);
  const Probe pt = probePin(GPS_PIN_TX, GPS_PROBE_MS);
  Serial.printf("[gps] pin probe: IO%d (RX1) edges %u high %u%% | IO%d (TX1) edges %u high %u%%\n",
                GPS_PIN_RX, (unsigned)pr.edges, (unsigned)pr.highPct,
                GPS_PIN_TX, (unsigned)pt.edges, (unsigned)pt.highPct);
  if (pr.edges < 20 && pt.edges >= 20) {
    s_rxPin = GPS_PIN_TX; s_txPin = GPS_PIN_RX;
    Serial.printf("[gps] GPS data is on IO%d: TX/RX wires are SWAPPED - using it anyway "
                  "(fine to leave as is)\n", GPS_PIN_TX);
  } else if (pr.edges >= 20) {
    Serial.printf("[gps] GPS data seen on IO%d (correct pin)\n", GPS_PIN_RX);
  } else if (pr.highPct > 90 || pt.highPct > 90) {
    Serial.println("[gps] lines idle HIGH, no data during the probe: the GPS may still be "
                   "starting up (normal after power-on); the baud search keeps listening");
  } else {
    Serial.println("[gps] lines LOW and silent: GPS unpowered or GND missing (or still "
                   "starting); the baud search keeps listening");
  }
  tryBaud(0);
  return true;
}

void update() {
  const uint32_t now = millis();
  // Baud search / link loss.
  if (!s_locked && now - s_tryStart > GPS_DETECT_MS) tryBaud((s_baudIdx + 1) % NBAUDS);
  if (s_locked && now - s_fix.lastMs > GPS_LOST_MS) {
    Serial.println("[gps] link lost - searching baud again");
    s_locked = false;
    s_fix.link = false;
    s_fix.baud = 0;
    tryBaud(s_baudIdx);                        // re-listen at the last good baud first
  }

  if (s_locked && !s_upgradeTried && s_fix.baud && s_fix.baud < 115200) {
    s_upgradeTried = true;
    requestBaud115200();
  }

  int budget = 1200;                           // bytes per call; plenty at 115200 / 40 ms
  if (U.available()) {
    if (s_lastByteMs && now - s_lastByteMs > 3000) {
      ++s_silences;
      Serial.printf("[gps] data resumed after %.1f s of SILENCE (#%u) - the module stopped "
                    "sending entirely\n", (now - s_lastByteMs) * 0.001f, s_silences);
    }
    s_lastByteMs = now;
  }
  while (U.available() && budget--) {
    const char c = (char)U.read();
    ++s_bytes;
    ++s_winBytes;
    ubxByte((uint8_t)c);
    if (c == '$') { s_len = 0; continue; }
    if (c == '\r' || c == '\n') {
      if (s_len > 0) { s_line[s_len] = 0; handleSentence(); }
      s_len = 0;
      continue;
    }
    if (s_len < (int)sizeof s_line - 1) s_line[s_len++] = c;
    else s_len = 0;                            // overlong: drop
  }

  // HUD text + serial status every GPS_REPORT_MS.
  if (now - s_lastStatusMs >= GPS_REPORT_MS) {
    const uint32_t win = now - s_lastStatusMs;
    s_lastStatusMs = now;
    s_fix.rateSps = (uint16_t)(s_winSent * 1000 / (win ? win : 1));
    s_fix.bps = (uint16_t)(s_winBytes * 1000 / (win ? win : 1));
    s_fix.types = s_winTypes;
    s_fix.ubxPerS = (uint16_t)(s_winUbx * 1000 / (win ? win : 1));
    s_fix.pvtPerS = (uint16_t)(s_winPvt * 1000 / (win ? win : 1));
    s_ubxText[0] = 0;
    for (int k = 0; k < s_nUbxSeen; ++k) {
      char item[16];
      snprintf(item, sizeof item, "%02X-%02X x%u ", s_ubxSeen[k].cls, s_ubxSeen[k].id, s_ubxSeen[k].n);
      if (strlen(s_ubxText) + strlen(item) < sizeof s_ubxText) strcat(s_ubxText, item);
    }
    s_nUbxSeen = 0;
    s_winUbx = s_winPvt = 0;
    s_winSent = s_winBytes = 0;
    s_winTypes = 0;
    static const char* const NAMES[8] = { "RMC", "GGA", "GSV", "GSA", "VTG", "GLL", "TXT", "?" };
    s_types[0] = 0;
    for (int i = 0; i < 8; ++i)
      if (s_fix.types & (1 << i)) { strcat(s_types, NAMES[i]); strcat(s_types, " "); }
    if (!s_fix.link) snprintf(s_status, sizeof s_status, "GPS no data (%u)", (unsigned)BAUDS[s_baudIdx]);
    else if (!s_fix.quality) snprintf(s_status, sizeof s_status, "GPS %u/%u heard, no fix", s_fix.satsHeard, s_fix.satsView);
    else snprintf(s_status, sizeof s_status, "GPS %u sats FIX", s_fix.satsUsed);
    if (s_fix.link)
      Serial.printf("[gps] %u baud | fix q%u %s | sats used %u view %u | hdop %.1f | %.6f, %.6f | "
                    "heard %u best %u dB | alt %.0f m | %.1f m/s | UTC %02u:%02u:%02u | ok %u bad %u | "
                    "%u sent/s %u B/s [%s] | UBX %u/s PVT %u/s fixType %u hAcc %.0f m [%s]\n",
                    (unsigned)s_fix.baud, s_fix.quality, s_fix.valid ? "A" : "V", s_fix.satsUsed,
                    s_fix.satsView, s_fix.hdop, s_fix.lat, s_fix.lon, s_fix.satsHeard, s_fix.snrMax,
                    s_fix.altM, s_fix.speedMS,
                    s_fix.hh, s_fix.mm, s_fix.ss, (unsigned)s_fix.sentences, (unsigned)s_fix.badSum,
                    s_fix.rateSps, s_fix.bps, s_types, s_fix.ubxPerS, s_fix.pvtPerS, s_fix.fixType,
                    s_fix.hAccM, s_ubxText);
    else
      Serial.printf("[gps] no NMEA yet (trying %u baud) | raw bytes %u%s\n",
                    (unsigned)BAUDS[s_baudIdx], (unsigned)s_bytes,
                    s_bytes ? " (data but no valid sentence: baud search continues)"
                            : " (nothing on RX: check power, GND, and GPS TX -> IO18)");
  }
}

const Fix& fix() { return s_fix; }
const char* statusText() { return s_status; }
void setEcho(bool on) { s_echo = on; }
const char* typesText() { return s_types; }
const char* ubxText() { return s_ubxText; }

}
