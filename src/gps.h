// gps.h — GPS receiver on UART1 (J4 / UART1 header: GPIO18 = RX1, GPIO17 = TX1).
// G0 bring-up: finds the module's baud rate, parses NMEA (GGA, RMC, GSV), and reports
// fix / satellites. Gameplay still uses the simulated locator; R2 feeds this into it.
#pragma once
#include <stdint.h>

namespace gps {

struct Fix {
  bool     link;        // valid NMEA sentences arriving (wiring + baud OK)
  uint32_t baud;        // detected baud (0 = still searching)
  uint8_t  quality;     // GGA fix quality: 0 none, 1 GPS, 2 DGPS, ...
  bool     valid;       // RMC status 'A'
  uint8_t  satsUsed;    // GGA
  uint8_t  satsView;    // GSV (all constellations, last 1 s epoch)
  uint8_t  satsHeard;   // GSV entries with a signal (C/N0 > 0) in the last epoch
  uint8_t  snrMax;      // best C/N0 in dB-Hz in the last epoch (~>30 = usable, >40 = good)
  float    hdop;        // GGA
  double   lat, lon;    // degrees (valid only when `valid`); double: GPS needs it
  float    altM;        // GGA, metres
  float    speedMS;     // RMC, m/s
  float    courseDeg;   // RMC
  uint8_t  hh, mm, ss;  // UTC from RMC
  uint32_t sentences;   // good sentences (checksum OK)
  uint32_t badSum;      // checksum failures
  uint32_t lastMs;      // millis() of the last good sentence
  uint16_t rateSps;     // good sentences per second (last report window)
  uint16_t bps;         // raw bytes per second (9600 baud tops out near 960)
  uint8_t  types;       // bitmask of sentence types seen in the window: 1 RMC, 2 GGA,
                        // 4 GSV, 8 GSA, 16 VTG, 32 GLL, 64 TXT, 128 other
  // UBX binary (G0-R5): FPV modules like the HGLRC M100 ship configured for UBX.
  uint16_t ubxPerS;     // valid UBX frames per second (last window)
  uint16_t pvtPerS;     // UBX-NAV-PVT frames per second
  uint8_t  fixType;     // NAV-PVT: 0 none, 2 2D, 3 3D (when NAV-PVT is present)
  float    hAccM;       // NAV-PVT horizontal accuracy estimate, metres
};

bool init();            // start UART1 and baud detection
void update();          // non-blocking: drain the UART, parse; call every loop
const Fix& fix();
const char* statusText();   // short line for the HUD: "GPS 7 sats 3D" / "GPS no data"
void setEcho(bool on);      // G0: also print raw GSV/GGA/TXT sentences to serial
const char* ubxText();      // e.g. "01-07 x10 01-35 x1" (UBX class-id seen recently)
const char* typesText();    // e.g. "RMC GGA GSV" for the types seen recently

}
