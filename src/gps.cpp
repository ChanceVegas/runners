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
uint8_t  s_gsvCount = 0;           // sats-in-view accumulator across GSV talkers
uint32_t s_lastStatusMs = 0;
uint32_t s_bytes = 0;              // raw bytes received (wiring check: 0 = nothing on RX)
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
  s_fix.lastMs = millis();
  if (!s_locked) {
    s_locked = true;
    s_fix.baud = BAUDS[s_baudIdx];
    Serial.printf("[gps] LINK OK at %u baud (first sentence: $%.5s)\n", (unsigned)s_fix.baud, s_line);
  }
  s_fix.link = true;

  char* f[24];
  const int n = split(s_line, f, 24);
  const char* type = f[0] + 2;                 // skip talker ("GP", "GN", "GL", ...)
  if (!strcmp(type, "GGA") && n >= 10) {
    s_fix.quality = (uint8_t)atoi(f[6]);
    s_fix.satsUsed = (uint8_t)atoi(f[7]);
    s_fix.hdop = (float)atof(f[8]);
    s_fix.altM = (float)atof(f[9]);
    if (s_fix.quality) { s_fix.lat = nmeaDeg(f[2], f[3]); s_fix.lon = nmeaDeg(f[4], f[5]); }
  } else if (!strcmp(type, "RMC") && n >= 9) {
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
    // $xxGSV,total,msgNum,satsInView,...: sum the in-view counts across talkers once
    // per cycle (msgNum 1 of each talker), publish when a GP set starts again.
    if (atoi(f[2]) == 1) {
      if (f[0][1] == 'P' && s_gsvCount) { s_fix.satsView = s_gsvCount; s_gsvCount = 0; }
      s_gsvCount += (uint8_t)atoi(f[3]);
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
    Serial.println("[gps] a line is held HIGH but silent: GPS powered but not sending yet, "
                   "or that wire is on a 3V3 hole");
  } else {
    Serial.println("[gps] NO activity on IO18 or IO17: GPS unpowered, GND missing, or the "
                   "data wires are on the wrong holes");
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
    tryBaud(0);
  }

  int budget = 1200;                           // bytes per call; plenty at 115200 / 40 ms
  while (U.available() && budget--) {
    const char c = (char)U.read();
    ++s_bytes;
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
    s_lastStatusMs = now;
    if (!s_fix.link) snprintf(s_status, sizeof s_status, "GPS no data (%u)", (unsigned)BAUDS[s_baudIdx]);
    else if (!s_fix.quality) snprintf(s_status, sizeof s_status, "GPS %u sats, no fix", s_fix.satsView);
    else snprintf(s_status, sizeof s_status, "GPS %u sats FIX", s_fix.satsUsed);
    if (s_fix.link)
      Serial.printf("[gps] %u baud | fix q%u %s | sats used %u view %u | hdop %.1f | %.6f, %.6f | "
                    "alt %.0f m | %.1f m/s | UTC %02u:%02u:%02u | ok %u bad %u\n",
                    (unsigned)s_fix.baud, s_fix.quality, s_fix.valid ? "A" : "V", s_fix.satsUsed,
                    s_fix.satsView, s_fix.hdop, s_fix.lat, s_fix.lon, s_fix.altM, s_fix.speedMS,
                    s_fix.hh, s_fix.mm, s_fix.ss, (unsigned)s_fix.sentences, (unsigned)s_fix.badSum);
    else
      Serial.printf("[gps] no NMEA yet (trying %u baud) | raw bytes %u%s\n",
                    (unsigned)BAUDS[s_baudIdx], (unsigned)s_bytes,
                    s_bytes ? " (data but no valid sentence: baud search continues)"
                            : " (nothing on RX: check power, GND, and GPS TX -> IO18)");
  }
}

const Fix& fix() { return s_fix; }
const char* statusText() { return s_status; }

}
