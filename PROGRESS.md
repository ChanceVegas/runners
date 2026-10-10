# PROGRESS.md — Single Source of Truth Across Sessions

Every session: read this first, update it last. If it isn't logged here, the next
session doesn't know it happened.

## Current State
- Phase: B1 battles + S1 shop + M1 music + rank + pass + VOL COMPLETE ✅ (2026-10-08,
  VOL-R1 on hw, user: "everything seems to be working great"; level 10 "very very
  minimal distortion" — caps kept). AU0 audio ✅. R3 ✅. P1 ✅. R1 ✅.
  A1-A3 art ✅. G0 GPS: UART link ✅, no fix, ON HOLD (GPS-PWR). L1 levels IN PROGRESS.
- Builds: yes (espressif32@6.5.0; RAM ~9.3%, Flash 36.7% of 1.3 MB app partition)
- Runs on hardware: yes — fps 25.0–25.4 in every mode.
- Measured (RK-R2 log, 2026-10-08): render ms avg MENU ~33 (max 35), EXPLORE 26–28
  (max 33), BATTLE run 26–29 (max 35, countdown 32–36), PREBATTLE ~24. Budget 39.6 ms:
  peaks leave only ~4 ms. A1 art MUST be measured against this (see ART-PERF).
  Heap flat ~269 KB (−7 KB audio, −1 KB shop/music vs B1).

## Next Up (in order)
1. L1A (user): `git pull`, flash, confirm `=== Runners L1A ===`. Existing rank points
   convert once (serial `[game] migrated rank points -> N XP`).
   - [ ] Menu shows LEVEL n (not RANK); battle countdown shows YOUR LEVEL n.
   - [ ] End screens show `+N XP  LEVEL n (x / y)` or `LEVEL UP!`; losing never drops level.
   - [ ] Serial `[game] +N XP -> total, level a -> b` after each battle.
   - [ ] Battles feel the same at low level (L1-L5 ~ old rank 1-2).
   Then L1b (start positions), L1c (map XP bar), E1 (3 new enemies).
2. GPS: ON HOLD (user 2026-10-09) — buying a different module. Suggested first: power
   the M100 from 5 V USB as a free test (GPS-PWR).
   A3 map art: PASSED (user: "map art tiles look perfect").
   Then: R2 proper (GPS drives the locator + approved Safety items 1–5) — breakdown first.

## Known Issues / Risks
- PWR-1 (open, hardware, 2026-10-09): battery power via IP5310 boost board (1S LG 18650
  1490 mAh). CrowPanel J1 (BAT) connector was torn off; its "+" pad is damaged. J1's big
  corner tabs are GND (schematic) - a wire there shorted the boost output. Current wiring:
  boost 5V -> D2 (SS14) ANODE = BAT+ net (diode test 0.209 V); boost GND -> J4 GND; M100
  VCC on boost 5V. Boost board trips at once even so; user suspects the board, has a
  replacement. Not yet verified: no-short (BAT+ to GND) with the board on USB.
 on J4 3V3 the M100 resets every ~26-60 s (10.4 s silence,
  RAM config lost) and its GSV reports 0 satellites in view on every cycle. Software
  can't fix it; G0-R7 constellation cut changed nothing. Next: 5 V supply.
- ART-PERF (watch; A2 log 2026-10-08: battle run ≤ 31 ms with the big pursuer, but
  countdown peaked 37 ms and MENU 35 — text-heavy frames, ~2.5 ms from the budget). A1 sprites cost ~0.5–1 ms (ARCADE run 25–26 ms vs ~24.5). The
  peaks are still TEXT-heavy frames, not art: MENU and battle countdown hit 35–36 ms of
  39.6. A2's big pursuer sprite must stay within its current footprint; if peaks
  pass ~37 ms, cache the static HUD/menu text (pre-rendered labels).
- VOL-CAP (closed): level 10 has "very very minimal distortion" on the salvaged
  speaker; caps kept (SFX 43%, music 23%).
- DUCK-1 (closed 2026-10-08): RK logs show deliberate ducks at y236–241, jumps
  y181–201; split 228 works.
- B1-TUNE (open): battle numbers are guesses, all in config.h. B1-R2 values: Pursuit
  walls 12%, rows 1.35x farther apart; Hunt HP 4/6/6, attack 1.6/1.8/1.4 s, orb
  75/80/60%, timer = fastest kill x 1.6.
- B1-CAUGHT (closed B1-R3): user — CAUGHT and GOT AWAY both lose the battle's coins
  (wallet untouched).
- B1-WALLS (closed B1-R3): user — walls replaced by duck-under bars (new DUCK control);
  walls kept rare (arcade 15%, Pursuit 8%, Phantom 1-in-3).
- DUCK-1 (open): AU0-R1 log — jump taps at y189-206; the one at y206 ducked into a
  barrier (split was 205). No deliberate duck in the log. B1-R5: split 228. B1-R3 log — both duck taps were meant as jumps (user: old habit of
  tapping low); they hit barriers. B1-R4 moved the split to y 205, added always-on
  ticks + distinct duck-bar look, and logs tap x/y. Tune from the next log.
- B1-ART (by design): enemies are placeholder shapes until A2.
- R3-SIM (by design, until R2): position is SIMULATED from drag (6x time scale);
  spawn windows use a boot-relative clock, so two devices won't share windows yet.
  R2 replaces both (GPS position + GPS UTC) behind locator / world::window().
- R3-RARE (design): Phantoms are hidden unless the player is running; running
  "reveals" them for the rest of their window so stopping to engage works. Reveal +
  escape memory is RAM only (OW_DEFEATED_SLOTS ring), lost on reboot — acceptable
  since windows are short.
- R3-BLOCK (closed 2026-10-07): terrain blocking dropped by user — the approved
  Safety section (PLANNING) addresses the real concern. All terrain walkable.
- R3-PERF (watch; feels smooth on hw 2026-10-08, numbers not yet captured): EXPLORE draws the full-screen terrain per row into the band
  buffer (no per-tile calls); the tile cache re-evaluates noise only when the view
  crosses a tile edge. Measure render ms in EXPLORE at the gate.
- TAP-1 (watch): R1-R3 test crash at 236 m — BARRIER in runner's lane, jumpY 0, no
  tap logged after 195 m. Either no jump was attempted or a middle tap was missed.
  If a "tapped but nothing happened" case is ever confirmed, check the one-sample
  bounce discard in input.cpp first (a very light/short tap may read as a bounce).
- CLEAN-1 (fixed P1): "LGFX_USE_V1 redefined" warning — display.h now guards it.
- P1-NVS (accepted): best score is written to flash (Preferences/NVS) inside the
  game loop, but only once per game over, and only on a new best. Can stall a frame
  for a few ms when it happens; invisible on the game-over screen.
- P1-AUDIO (closed 2026-10-08, AU0 hw-verified: "audio sounds great"; fps 25.3,
  render unchanged, heap −7 KB for I2S driver + task, stable): speaker fitted by user 2026-10-08 (salvaged LilyGO
  T-Deck Pro speaker, 7.2 ohm DC = 8 ohm nominal) on the board's amp output. I2S pins
  BCLK 35 / LRCK 19 / DOUT 20 are from an Elecrow forum reply, NOT official docs.
  Amp chip unidentified. Volume capped at 30% (AUDIO_VOLUME_PCT) until the user
  confirms it sounds clean.
- P1-ART (open): all art is still primitive shapes (rects/ellipses/triangles). Real
  sprites: A1 adds runner/obstacles/coin/orb; enemies, HUD and shop art are A2.
- R1-BYTE (closed, data): LovyanGFX 16-bit sprite buffers store RGB565 BYTE-SWAPPED
  (boot self-test: red 0xF800 reads 0x00F8). Any direct write into a band buffer
  must swap. lanes::init() detects it and converts; colours verified on hardware.
- R1-FEEL (CLOSED, hw-verified R1-R3 2026-10-07: "feels much improved"; every tap in
  the test log registered): controls felt sluggish; four deaths at
  exactly 59 m = the FIRST obstacle row, which gives ~4 s warning → inputs weren't
  registering, not a reaction-time problem. Root cause: touch read inside the logic
  ticks, which run in a burst before each frame → effectively 25 Hz sampling (the
  INPUT-2 limit documented in Cave Escape). Fix: dedicated 100 Hz sampler task on
  core 1 running the gesture state machine; edges counted and delivered one per
  tick (swipes queue, so a double swipe moves two lanes). Jump speed now measured
  over a 40 ms window. Swipe 40 → 30 px, lane change 143 → 100 ms. Serial logs each
  input and crash cause (DEBUG_INPUT_LOG). Then (2026-10-06, before R1-R2 was
  play-tested) user switched to TAP ZONES: gestures need several samples to
  recognise, a zone tap fires on the 2nd sample (10 ms after touch-down; the first
  resistive sample is unreliable, one-sample bounces are discarded). Learned from
  touchscreen Game Boy emulators on the CYD (on-screen buttons, not gestures).
  Tunables: INPUT_ZONE_LEFT_X / RIGHT_X, RUN_LANE_SPEED, RUN_SPEED_*, OBST_GAP_*.
- R0-3 (fixed, hw-verified R1-R1: fps 25.3): frame pacer from Cave Escape main.cpp was not
  carried over; R0-R2 rendered unpaced at 43 fps against a 25.26 Hz panel. Restored.
- TOUCH-1 (closed, data): absolute touch mapping verified 2026-10-05, orientation
  correct, no mirroring. Readings: TL 22,16 · TR 447,37 · BL 19,232 / 30,235 ·
  centre 257,138 (no BR reading). Edge readings fall ~20–35 px short of the true
  edge (finger can't hit the corner pixel). Keep touch targets ≥ ~40 px; calibrate
  only if fine UI ever needs it.
- R0-1 (fixed, hw-verified R0-R2): bars jerked backward — pattern period 64 px but scroll
  wrapped at 32, swapping the two shades each cycle.
- R0-2 (fixed, hw-verified R0-R2): FOUR green dots per touch. main.cpp read the touch
  controller a second time (raw, unsmoothed) outside input::. Now one read per tick,
  marker uses input's smoothed point. If multiple dots persist, absolute touch
  mapping is wrong: Cave Escape only verified RELATIVE drag, never absolute position
  — corner xy readings will show mirroring/scaling.
- CARRY-1 (closed by design, A1): the A1 blitter (sprite.*) never uses pushImage — it
  writes the band buffer directly with the palette converted once by renderer::raw(),
  the same verified path as the road. Magenta/colour-key failure can't occur there.
  If wrong colours ever show on sprites, check sprite::init ran after renderer::init.
- HW-SUN (open, design): CrowPanel display not sunlight-readable; outdoor play needs
  different field hardware (PLANNING Open Decision #2).
- GPS-INDOOR (open, process): GPS rarely fixes indoors. R2 must ship a fake-GPS
  replay mode so overworld work doesn't require walking outside.
- R4-HW (open): player-to-player testing needs two boards + two GPS units.

## Decisions Made
- 2026-10-08: Battle/Shop/Art spec approved (PLANNING). Order: B1 battle -> S1 shop ->
  A1 art foundation -> A2 art. Battle replaces chase mode; Arcade unchanged.
- 2026-10-07: Safety items 1–5 approved for R2 (startup notice, no input while moving,
  audio enemy cue, speed lockout ~7 m/s, simplified moving view). Terrain blocking
  dropped. OpenStreetMap deferred (microSD would be the route if ever revisited).
- 2026-10-07: User: build the overworld (R3) in one iteration, before GPS (R2).
  Position simulated behind the locator API. Menu with EXPLORE / ARCADE modes.
- 2026-10-07: Claude GitHub App installed by user → Claude pushes directly to main
  (CLAUDE.md Transfer Protocol rewritten). User: `git pull` before flashing.
- 2026-10-05: Cave Escape scrapped by user; archived at tag `cave-escape-final`.
- 2026-10-05: New game "Runners": GPS overworld + pseudo-3D lane encounters.
  Encounters start only when stopped. Deterministic world from GPS cell hash.
- 2026-10-05: Player link = ESP-NOW (no router/server); BLE reserved for phone app.
- 2026-10-05: Prototype on CrowPanel 4.3; field hardware chosen later.
- 2026-10-05: GPS on UART1 header (GPIO18 RX / GPIO17 TX), u-blox M10 recommended.
- Carried from Cave Escape: PlatformIO + Arduino core 2.0.14 (pinned), LovyanGFX,
  band compositor, drag-gesture input, 3-doc process, transfer protocol.

## Session Log (newest first)
### 2026-10-09 — Session 3 (cont.) — battery power wiring (hardware only)
- User wired the IP5310 boost board + 18650 to feed the CrowPanel BAT input and the M100
  at 5 V (plan: boost button = hard power switch, later IO38 soft power button). J1 got
  damaged; rerouted BAT+ to D2's anode. Boost board dies immediately; replacing it.
  See PWR-1. No code changes. L1A still awaiting the user's hardware test.

### 2026-10-09 — Session 3 (cont.) — L1 levels + XP
- User: hold GPS (will buy a 3.3 V module; I suggested a 5 V USB test first since the
  M100 is already 3.3 V rated). Replace rank with XP/levels: no XP loss, max 50,
  level-ups should give something (TBD), start beside at L15 / ahead at L30, +3 enemies
  unlocked by level (Claude to propose). Rewards + escape rules: OPEN.
- Done: L1a — battle levelOf/xpToReach/xpGain, difficulty d replaces rank in all
  scaling, game_state saves "xp" (migrates "rankpts"), menu LEVEL, countdown YOUR
  LEVEL, end screens +XP / LEVEL UP. Banner L1A.
- Commit: feat(battle): levels + XP replace rank (L1A)

### 2026-10-09 — Session 3 (cont.) — G0-R7 result
- G0-R7 log: config ACCEPTED (ACK 06-8A) every relink, GSV now present: `$GPGSV,1,1,00`
  and `$GAGSV,1,1,00` = 0 satellites tracked. Still 10.4 s silence every ~26 s. User
  read this as "no link"; the link is fine, there is no satellite reception.
- Done: G0-R8 — dropped the GLONASS/BeiDou/QZSS-off keys (no effect; constellation
  changes force a GNSS restart). Kept GSV + UBX ACK as diagnostics.
- Commit: diag(gps): drop constellation keys, keep GSV/ACK diag config (G0-R8)

### 2026-10-08 — Session 3 (cont.) — GPS low-power experiment
- User reseated the jumpers; asked to try software first (no good 5 V source yet; has a
  5 V 1 A boost module — explained safe wiring: VIN from J4 3V3/GND, VOUT+ to GPS VCC
  only, verify ~5.0 V before connecting).
- Done: G0-R7 — after every (re)link or silence, RAM-only CFG-VALSET: UBX out on (ACK/
  NAK logged), GSV on, GLONASS/BeiDou/QZSS off (GPS+Galileo only, less acquisition
  current). Baud upgrade now off by default (GPS_UPGRADE_BAUD 0).
- Commit: feat(gps): low-power GNSS config experiment, ACK/NAK logging (G0-R7)

### 2026-10-08 — Session 3 (cont.) — GPS keeps restarting (power)
- G0-R5 outdoors (photo of the detail panel + log): NO UBX at all (my UBX theory was
  wrong); the module sends only RMC + GGA (~3 sentences/s, ~118 B/s), no GSV.
  The 115200 request WORKED (link at 115200, first sentence $GNTXT) but ~12 s later the
  module went silent and came back at 9600 = it RESTARTED (RAM settings lost). Every
  ~40-60 s: 10-15 s of total silence (0 B/s), then RMC/GGA again with no time/fix.
  UTC appeared once (23:31:11) just before the log ended = it can hear satellites when
  it stays up long enough. Diagnosis: brown-out resets at 3.3 V supply (M10 FPV modules
  regulate internally and are usually fed 5 V).
- Also: the GPS detail panel costs ~12 ms render (39-40 ms, 24 fps while open) — debug
  only; fine to leave, or trim later.
- Done: G0-R6 — logs every silence + "module came back at N: it restarted", always
  prints the module's $TXT lines. Asked the user to feed GPS VCC from 5 V.
- Commit: diag(gps): log silences, restarts and module text (G0-R6)

### 2026-10-08 — Session 3 (cont.) — GPS: module streams UBX, 9600 too slow
- User: outside, open sky, ~5 min, no fix; GPS blue LED on, red LED slowly flashing
  (red = PPS per a seller listing; may pulse without a fix). Log (G0-R2, likely indoors):
  ~4 NMEA sentences/s, 10 s gaps where 24.8 KB of non-NMEA bytes arrived, UTC never set.
  Seller listing: M100 Mini = UBX protocol, 10 Hz, 115200 baud, 3.3-5 V (rcdrone.top).
- Hypothesis: module streams UBX (+ some NMEA) faster than 9600 can carry.
- Done: G0-R5 — UBX parser (NAV-PVT -> fix/sats/lat/lon/speed/hAcc), UBX + NMEA rate
  and type stats, RAM-only request for 115200 (CFG-VALSET + legacy CFG-PRT) once linked
  below 115200, then follow; tap the map GPS box for a detail panel (works outdoors
  without a laptop).
- Commit: feat(gps): UBX NAV-PVT parser, 115200 baud request, detail panel (G0-R5)

### 2026-10-08 — Session 3 (cont.) — no satellite lock
- User: "i am not getting any satellite lock" (location/duration/log not yet given).
- Done: G0-R4 diagnostics — talker-agnostic per-epoch GSV stats (in view, heard =
  C/N0 > 0, best C/N0 dB-Hz), HUD "GPS heard/view", raw GSV/GGA/TXT echo of one epoch
  every 10 s (u-blox TXT lines can report antenna status).
- Likely causes to rule out: indoor/obstructed sky, cold start without backup battery
  (minutes), RF noise from the display/ESP32 right under the antenna, 3.3 V supply on
  a module whose sellers often spec 5 V.
- Commit: diag(gps): satellite signal stats + raw NMEA echo (G0-R4)

### 2026-10-08 — Session 3 (cont.) — GPS link up
- User swapped the two data jumpers ("I think the wires are just swapped" — correct).
  G0-R2 log: LINK OK at 9600 ($GNRMC), ok 87 bad 0; indoors 0 sats / no fix. The boot
  pin probe had shown no edges on either pin — it ran while the GPS was still booting,
  so it is NOT a reliable wiring verdict (texts softened). One link loss after ~12 s
  (~5 s pause) recovered at 9600.
- Done: G0-R3 — GPS_LOST_MS 10 s, re-listen at the last good baud first, probe texts.
- Commit: fix(gps): patient link-loss handling, softer probe verdicts (G0-R3)

### 2026-10-08 — Session 3 (cont.) — G0-R1 hw: no GPS data
- User: "map art tiles look perfect" (A3 passed). G0-R1 log: link never found, raw
  bytes 0 across all 6 bauds for several minutes -> no signal reaches IO18 (wiring,
  power, or swapped TX/RX; not a software baud problem). MENU 33–34 ms, SETTINGS 21.
- Done: G0-R2 boot pin probe (edges/high% on IO18 and IO17), auto-swap RX to IO17 if
  the GPS data is there. Asked user: GPS LED on? 3.3 V at the GPS? meter check of J4.
- Commit: diag(gps): boot pin probe + auto TX/RX swap (G0-R2)

### 2026-10-08 — Session 3 (cont.) — G0 GPS bring-up
- User wired the HGLRC M100 Mini to the J4 socket (photo: J4 = 3V3,3V3,GND,GND,
  IO18-RX1,IO17-TX1,IO38,IO37). Module: 3.3-5 V, NMEA, 9600 default (Cirkit Designer).
- Done: gps.* (UART1 18/17, RX buffer 1 KB, baud autodetect 9600/38400/115200/57600/
  19200/4800, checksum-verified GGA/RMC/GSV, link-loss re-search, 2 s serial status
  with wiring hints), map HUD GPS line. Locator untouched (still simulated).
  Flash 39.0%. Banner G0-R1.
- Commit: feat(gps): G0 bring-up — UART1 NMEA parser, baud autodetect, status

### 2026-10-08 — Session 3 (cont.) — A3 map art
- User chose map art next (and was hitting obstacles on purpose in the A2 log).
- Done: art_gen A3 — 22 terrain colours, 13 tiles (grass x3, sand x2, water x2 frames,
  forest x2, rock x2, trail x2; seeded, opaque 24x24) + avatar down/up/side x 2 walk
  frames. overworld: tile rows copied through sprite::rawPalette() (old fill/canopy
  code and its palette removed), avatar sprite by heading, map enemies = front art at
  0.9x, top-bar coin/shield icons. art_data 22.4 KB; Flash 38.3%. Banner A3-R1.
- Commit: feat(art): A3 overworld tiles, map avatar and enemies

### 2026-10-08 — Session 3 (cont.) — A2 on hw
- User: "shop text looks excellent". Log: Brute L2 at rank 5 (HP 8, 38.7 s timer): 3 orb
  hits, then CAUGHT by duck-bar hits (one duck tapped 1 m late, one mid lane change).
  fps 25.0–25.5; battle run 26–31 ms; countdown max 37. A2 closed.

### 2026-10-08 — Session 3 (cont.) — A2 art
- User approved A2 ("works for me. let's work!").
- Done: art_gen A2 — shape helpers + auto-outline; 10 new palette colours; Shade /
  Brute / Phantom back (2 frames) + front, knocked-down runner, heart (full/empty),
  shield, 3 sneakers, 3 cans (29 sprites, 13.8 KB). sprite::draw gains a solid-colour
  mode (hit flash). battle draws enemies with sprites (same footprints as B1 shapes),
  encounter down pose / shield / coin icons, shop + pre-battle card pictures.
  Flash 37.6%. Banner A2-R1.
- Commit: feat(art): A2 enemies, knocked-down runner, HUD icons, shop pictures

### 2026-10-08 — Session 3 (cont.) — A1 render log
- Log (second half; first half was an older capture): ARCADE run 25.0–25.8 ms avg,
  BATTLE run 26–29, countdown 34–35 (max 36), MENU max 36, EXPLORE 25–28; fps 25.0–25.4;
  heap 269,196 flat. Rank 4 -> 5. A1 closed. A2 breakdown awaiting approval.

### 2026-10-08 — Session 3 (cont.) — A1 on hw
- User: "love it!!!! first art iteration looks wonderful!!" Render ms not yet sent.
  Next: A2 breakdown for approval.

### 2026-10-08 — Session 3 (cont.) — A1 art foundation
- User approved A1 ("Let's Grind!").
- Done: tools/art_gen.py (text-grid + generated pixel art -> src/art_data.h, one shared
  27-colour palette, 5.7 KB; --preview writes a sprite sheet PNG, gitignored),
  sprite.* (direct band-buffer blit, 16.16 nearest-neighbour scale, flip, transparent
  index 0), runner (run A/B + mirror, jump, duck, stumble) at 2x, barrier / duck bar /
  wall / coin / orb sprites sized within their collision boxes. CARRY-1 closed by
  design. Flash 37.0%. Banner A1-R1.
- Commit: feat(art): A1 sprite pipeline + runner, obstacles, coin, orb

### 2026-10-08 — Session 3 (cont.) — VOL-R1 hw result: milestone wrap
- User (VOL-R1 on hw): "everything seems to be working great"; level 10 "very very
  minimal distortion". B1/S1/M1/rank/pass/volume marked complete. Next: A1 (needs
  breakdown approval), GPS check when soldered.

### 2026-10-08 — Session 3 (cont.) — VOL-R1 volume control
- User: need a way to control volume. Chose: settings screen + in-game mute.
- Done: audio levels 0-10 (live gains, mute), settings.* (screen, NVS vmus/vsfx/mute,
  mute icon + corner hit test), input reserves the top-right corner (no lane-right),
  menu SETTINGS button, Mode::Settings. Banner VOL-R1.
- Commit: feat(settings): music/sound volume + in-run mute (VOL-R1)

### 2026-10-08 — Session 3 (cont.) — RK-R3 pass the enemy, race restarts
- User (RK-R2 on hw): "feels better". Log: rank 2 Shade, 4 orb hits in ~8 s, defeated
  with 8.7 s left; rank 2 -> 3; RUSH drop. fps 25.2-25.4, render 26-35 ms (MENU 33).
- User: passing the enemy should restart the race meter, with "you passed <enemy>".
  Chose: pass = DEFEATED locked in, then escape. Done: pass -> Pursuit at 25 m, new
  finish +300 m (encounter::engine::restartRace, goal bar from the pass point),
  weakened enemy, half-HP re-Hunt, loss after a pass keeps the defeat bonus.
- Commit: feat(battle): pass the enemy, then the race restarts (RK-R3)

### 2026-10-08 — Session 3 (cont.) — RK-R2 Hunt "can't catch it"
- User (RK-R1 on hw): "hunt feels like the player can never catch the enemy".
- Causes found: (1) the enemy sat at a fixed 26 m — orb hits lowered HP but nothing
  on screen ever got closer; (2) the timer assumed EVERY orb collected (x1.6 slack),
  but orbs come only in free lanes while dodging, so it often ran out first.
- Done: enemy distance follows HP (30 m -> 11 m), "CAUGHT IT!" pass animation then
  DEFEATED (no collisions during it), attacks clamp to >= 18 m ahead, timer slack 2.4,
  orb pity, orb-hit log line. Banner RK-R2.
- Commit: fix(battle): Hunt closes in on each orb hit, fairer timer (RK-R2)

### 2026-10-08 — Session 3 (cont.) — runner rank (enemy scaling)
- User: permanent sneakers need enemies that get progressively harder; drinks one
  battle each (confirmed: already the case, kept as-is).
- User chose: rank from wins; losses CAN drop rank. Done: battle rank helpers
  (rankOf, ptsAfter, rewardMul) + per-rank scaling of gap gain/loss, speed, row
  density, HP, attack rate, orb rate; encounter rank line on countdown + end screens;
  game_state saves "rankpts", scales win bonus, menu shows RANK. Banner RK-R1.
- Commit: feat(battle): runner rank — enemies scale with rank from battles

### 2026-10-08 — Session 3 (cont.) — M1 music
- Done: tools/music_gen.py (8-bar song notation -> src/music_data.h, 4 x 128 rows),
  audio.* music sequencer (lead square w/ song duty + decay, bass square oct 3, kick
  sweep, noise snare/hat), music(Track) API, game_state::updateMusic() picks the
  track per mode and silences it on run end. Tracks: TITLE C major 140 bpm, EXPLORE
  G major 118, BATTLE A minor 160, ARCADE F major 150. MUSIC_VOLUME_PCT 16.
  Built: SUCCESS (Flash 36.3%). Banner M1-R1. Tunes are unheard by Claude.
- Commit: feat(audio): chiptune music — 4 loops + sequencer (M1)

### 2026-10-08 — Session 3 (cont.) — S1 shop
- User: GPS not soldered yet (GPS check waits). Wants game music + the store; order
  shop first, then music. Music: upbeat chiptune, loops for menu, explore, battle,
  arcade. Arcade coins: "yes, but reduced" -> 50% banked.
- Found uncommitted S1 edits (config tunables, jumpMul, drop display) from an
  interrupted attempt in this same session (no other session running); they matched
  the plan and were kept.
- Done: shop.* (inventory in NVS g0..g5, SHOP + PRE-BATTLE screens, takeBattleStats,
  defeat drop pre-rolled + granted on DEFEATED), game_state modes PreBattle/Shop,
  3-button menu, GUARD shield not refunded as energy, arcade coin banking.
  Built: SUCCESS (Flash 35.9%). Banner S1-R1.
- Commit: feat(shop): sneakers + drinks shop, pre-battle drink screen (S1)

### 2026-10-08 — Session 3 (cont.) — AU0 verified; B1-R5 duck split
- User (AU0-R1 + B1-R4 on hw): "game feels good. audio sounds great actually."
  Log: fps 25.2-25.4, render 26-28 ms in battle, heap 270,164 flat (−7 KB audio).
- Tap y: jumps 189-206, lanes 215-246. Duck at y206 was a jump -> barrier hit.
  Split moved 205 -> 228. Banner B1-R5.
- Commit: fix(input): duck split to y 228 from tap log (B1-R5)

### 2026-10-08 — Session 3 (cont.) — AU0 audio bring-up
- User has an HGL M100 Mini GPS module and a small speaker; speaker soldered to the
  board's amp output (from a LilyGO T-Deck Pro; reads 7.2 ohm = normal for 8 ohm).
  GPS not yet wired. Open question to user: GPS check before or after S1 shop.
- Done: audio.* (legacy IDF I2S, 16 kHz, 2-voice square/noise synth task on core 0,
  priority 4), board_config I2S pins corrected (19 = LRCK, not MCLK; from Elecrow
  forum), SFX hooked: boot chime, menu tap, jump, duck, coin, orb, hit, win, lose.
  Volume capped 30%. Flash +16 KB (35.6%). Banner AU0-R1.
- Commit: feat(audio): I2S SFX synth + game sound hooks (AU0)

### 2026-10-08 — Session 3 (cont.) — B1-R3 hw result -> B1-R4 duck tuning
- User: jump/duck "a little sketchy". Log: duck taps at 103 m and 495 m each followed
  by a BARRIER hit (meant as jumps — user confirmed old habit of tapping low); two
  other barrier hits had no tap. 25.3 fps, render 25–28 ms in battle.
- User chose: keep the split, tune it. Done: split y 170 -> 205, always-on split ticks
  during a run, duck bar restyled yellow/black + down-arrow, taps log x/y. Banner B1-R4.
- Commit: fix(input): lower duck strip, visible split, distinct duck bar (B1-R4)

### 2026-10-08 — Session 3 (cont.) — B1-R3 duck controls + coin loss
- User: CAUGHT loses the battle's coins too (wallet untouched). Replace walls with
  tall bars the player DUCKS under — everywhere (Arcade, Pursuit, Brute). Duck input =
  split middle zone (top = jump, bottom = duck).
- Done: input duckPressed (middle zone, y >= 170); encounter Block::Overhead (duck bar)
  + duck state (0.6 s, fast fall mid-air, jump cancels), crouch pose, hint row;
  spawn mix walls 15% / duck 35%+5%/stage (arcade), 8% / 35% (Pursuit); Brute smash
  = duck bars (1 in 3 full-width), Phantom random kind; lost battle = coins lost.
  Built: SUCCESS. Banner B1-R3.
- Commit: feat(encounter): duck control + duck-under bars replace most walls (B1-R3)

### 2026-10-08 — Session 3 (cont.) — B1-R1 hw feedback -> B1-R2 tuning
- User (B1-R1 on hw): Pursuit slightly too hard; big walls feel out of place; hits
  should be much more impactful; Hunt drags -> needs a time limit. Log: 25.3 fps all
  modes, render 24–32 ms.
- User decisions: walls = the out-of-place obstacle; impact = feel + fewer obstacles;
  keep HP + add a timer; timer out = enemy GOT AWAY, no bonus, battle coins lost;
  scoreboard tracks battles lost and enemies that got away.
- Done: Pursuit walls 12% + rows 1.35x apart; hit impact (lanes::setShake camera
  shake, thick red border, pursuer lunges, wall stumble 1.6 s, slower stumble);
  Hunt timer (Outcome::GotAway, GOT AWAY! screen, coins forfeited); HP lowered;
  NVS "lost"/"gotaway" + menu line. Built: SUCCESS. Banner B1-R2.
- Commit: feat(battle): hit impact, rarer Pursuit walls, Hunt time limit (B1-R2)

### 2026-10-08 — Session 3 (cont.) — B1 battle system
- User approved the spec ("Looks fine. Let's do it.").
- New module battle.* (rules + per-kind data table + placeholder drawing).
  encounter: Mode Battle, startBattle(), engine:: spawn API (obstacle/orb/clearAhead),
  orb pool, stumble (slow + invulnerable, no crash) in battle, enemy in the depth-sorted
  draw list, pursuer overlay, battle HUD, DEFEATED/ESCAPED/CAUGHT screens.
  game_state: Chase -> Battle rename, DEFEAT_BONUS_COINS, battle::Stats hook for S1.
- Built in container: SUCCESS (RAM 8.9%, Flash 34.1%). Pushed; awaiting hw gate.
- Commit: feat(battle): pursuit -> overtake -> hunt battles replace chase mode

### 2026-10-08 — Session 3 (cont.) — next milestones: battles, shop, art
- User: no speaker hardware yet (audio waits); start real art; add a shop (sneakers,
  energy drinks); chases don't feel like real battles; GPS still on hold.
- User's battle design: Pursuit (enemy behind, gap meter, stumbles cost ground) that
  becomes a Hunt (enemy overtakes, then attacks from ahead) — spec written into
  PLANNING "Battles, Shop, Art — PROPOSED". Sneakers = permanent stat tiers, drinks =
  one-use pre-battle boosts, art = bright chunky pixel art. Awaiting approval of the
  spec + order (B1 battle -> S1 shop -> A1/A2 art).

### 2026-10-08 — Session 3 (cont.) — R3 hardware result
- User: R3 feels smooth; menu works perfectly; buttons work; score data and stats
  tracked well. Logged R3 COMPLETE on that report. No serial timing captured (P1/R3).

### 2026-10-07 — Session 3 (cont.) — real-world safety requirement
- User clarified the blocking request is about safety: with GPS, players must stay
  aware of real surroundings (e.g. real water). Pointed out the map is fictional, so
  in-game blocking can't protect anyone and could create false trust. Logged a
  Safety section in PLANNING (notice, no input while moving, audio cue, speed
  lockout, simplified moving view; OSM as a future option). Awaiting approval.

### 2026-10-07 — Session 3 (cont.) — R3 flashed; impassable terrain request
- User flashed R3. Request: dense woods, rock and water should block the player
  (fix with GPS). Logged as R3-BLOCK / PLANNING Open Decision 7 with options; asked
  user to choose. Framework warning in esp32-hal-uart.c explained: Espressif core
  bug, harmless, not patched (pinned core).

### 2026-10-07 — Session 3 (cont.) — P1 hardware result
- User: P1 (P1-R2) running smoothly on the board. Logged P1 COMPLETE on that report;
  no serial numbers captured for P1 — collect render ms in ARCADE at the R3 gate.

### 2026-10-07 — Session 3 — R3 overworld in one iteration (R3-R1)
- User: attack the overworld map in one iteration. Built before GPS: position comes
  from a simulated source behind locator (drag = walk/run, 6x time scale, real-scale
  speeds reported so classes/energy/engage behave as with GPS).
- New modules:
  - locator.*: Pos = int32 tile + float offset (no float precision loss at GPS-scale
    coordinates), speed, Still/Walk/Run, heading, exact deltaM across tiles.
  - world.*: terrain = 2-octave value noise on INTEGER tiles with integer periods
    (exact for any absolute coordinate) + moisture + trail band → water / sand /
    grass / forest / rock / trail. Spawns: hash(cell, window) → presence (45%),
    kind (Shade / Brute 30% / rare Phantom 12%), tile in cell; never on water.
    Escaped + revealed memory ring.
  - overworld.*: camera on the player; terrain rows written straight into the band
    buffer (canopy circles, wave glints, flowers, rock speckle via span tables);
    visible-tile cache; enemies + avatar via primitives; engage = nearest enemy in
    14 m + still for 0.8 s; must move again after a chase; Run energy from running.
  - game_state.*: Menu (over the attract road) / Explore / Chase / Arcade; installs
    each mode's layer stack; chase handshake: shields = floor(energy/50) max 2, paid
    up front, unused refunded; escape bonus 25 coins x level; profile in NVS
    ("profile": wallet, escapes, energy, walked, tile) saved on mode changes and
    every 120 s exploring; first boot spirals to the nearest land tile.
  - hud.*: shared band-clipped rect/frame/text (extracted from encounter).
- Changed: encounter gains Arcade/Chase modes, shields with blink invulnerability,
  enemy label + shield pips in the HUD, MENU button on arcade game over, title text
  moved to the menu; renderer now owns the band byte-order test + raw(); input gains
  moveY (vertical drag axis).
- Host previews: world map render (46% grass, 25% forest, 9% trail, 8% water, 7% sand,
  5% rock; ~1 enemy per screen) and a screen-scale overworld frame — caught a
  chessboard look from alternate-tile shading; reduced to one green step on grass.
- Commits: refactor(r3): renderer owns byte order, shared hud helpers, input moveY
  / feat(r3): overworld — locator, world, map, enemies, chase mode, menu, profile

### 2026-10-07 — Session 2 (cont.) — P1 code review fixes (P1-R2)
- User code-reviewed the P1 patch; six findings, verified against the code:
  1. Blink read from millis() per band → a string spanning a band seam could render
     half-on on the flip frame. FIXED: d_blink latched in beginRender.
  2. Coin trails (15 m) vs late-game row gap (18 m): first coin landed 3 m past the
     previous row and led into that row's blocked lane. FIXED: trail clamped to
     prevRow + COIN_TRAIL_MARGIN_M (6 m); far coins dropped when the gap is short.
  3. Scenery MAX_PROPS guard load-bearing by one slot. FIXED: per-push bounds check.
  4. text() measured every string in every band. FIXED: band-miss test (font height
     only) now precedes textWidth(). Gate extended: read render ms on title/game-over.
  5. Arc coin (30 px) vs barrier clear (30 px): equal → fair; feel-check on hardware.
  6. s_travel unbounded in attract mode. FIXED: wrap by 660 m (whole stripe + curve
     cycles) past 100 km.
- Commit: fix(p1): review fixes — frame-latched blink, trail clamp, text band test

### 2026-10-07 — Session 2 (cont.) — P1 standalone polish pass
- User: hold GPS + overworld; make the bottom instructions bigger; get as close to a
  polished game as possible minus GPS in one pass.
- Done (P1-R1):
  - Fonts: LovyanGFX smooth FreeSansBold 9/12/18/24 pt replace the 8 px default.
    Instructions 12 pt (was 8 px). text() auto-shrinks anything wider than the
    screen; all strings measured against the real glyph widths (longest 459/480 px).
  - Flow: Ready (title + saved best) → Countdown 3-2-1-GO → Run → Crash (red flash,
    runner knocked flat, 0.9 s) → GameOver (score, NEW BEST!, stage/coins/metres) →
    tap → new game. 900 m → StageClear → tap → next stage (+3 m/s start+max, +5%
    walls, caps 38 m/s / 70%). Score = metres across stages + 10/coin.
  - Coins: 60% of rows get a 5-coin ground trail into the open lane, or (40% of
    those) a 3-coin arc over a barrier that needs a jump. Spinning gold ellipses.
  - Best score saved in NVS (Preferences "runners"/"best"), loaded at boot.
  - scenery.*: pines, tall pines, bushes, marker posts every 11 m per side (sides
    offset), placed by hash of slot index — stateless, deterministic.
  - Draw list: obstacles + coins + runner projected ONCE per frame and depth-sorted
    in beginRender (R1 re-sorted per band).
  - Runner redrawn (race bib, shoes, lean into lane changes, jogs in countdown).
  - Tap hints show on title, countdown and the first 5 s of a run, then hide.
  - CLEAN-1 fixed.
- Host preview checked composition (scenery off-road, coin arc height, HUD fit).
- Commit: feat(p1): standalone polish — fonts, coins, stages, best score, scenery

### 2026-10-07 — Session 2 (cont.) — GPS on hold; bigger instruction text (R1-R4, superseded)
- User: hold off on GPS and the overworld map.
- An interrupted first attempt at the text request (built-in font size 1 → 2, tag
  R1-R4) was left uncommitted in the container and never shipped. P1 replaced it
  with smooth FreeSans fonts; R1-R4 never reached hardware.
- Commit: feat(r1): larger instruction text; GPS + overworld on hold

### 2026-10-07 — Session 2 (cont.) — R1 COMPLETE
- R1-R3 tap zones on hardware: user "feels much improved". Log: 7 taps in one run
  (LEFT/RIGHT/MIDDLE mix), each received immediately; fps 25.2–25.3, render ~21.3 ms,
  heap flat 281,540. Best run 451 m at 97 km/h. One crash logged as TAP-1 (watch).
- R1 gate passed: ≥25 fps with compose ≤35 ms AND playable controls on resistive touch.
- Docs-only commit: docs(r1): R1 complete — tap zones hw-verified

### 2026-10-06 — Session 2 — tap zones (R1-R3)
- User asked how touchscreen emulators on the CYD stay responsive (cyd-gb). Answer:
  on-screen BUTTONS (state, fires on first sample) vs our GESTURES (need several
  samples to recognise). Also clarified PSRAM isn't Doom's limiter (CYD Doom has no
  PSRAM; our PSRAM holds the framebuffer and shares its bus with panel refresh); our
  headroom is fine — visual gap is art. User: Doom was only an example.
- User: switch to tap zones. Done: input.cpp gesture code replaced by zone taps
  (left/right third = laneStep ∓1, middle = jumpPressed, any = pressed), kept the
  100 Hz sampler task. laneSwipe renamed laneStep. Swipe/flick constants removed
  from config; INPUT_ZONE_LEFT_X/RIGHT_X added. HUD: zone dividers + "<  JUMP  >"
  hints along the bottom; title screen explains the zones. Serial tap log updated.
- Commit: feat(r1): tap-zone controls replace swipe/flick gestures

### 2026-10-05 — Session 1 (cont.) — R1-R1 hw results → R1-R2 input fix
- PASS: colours correct, fps 25.2–25.3 paced, render ~21 ms (budget 35), heap flat,
  full loop works (READY/RUN/LOSE/retry seen in log; best run 319 m).
- Byte order measured: band buffer stores swapped RGB565 (R1-BYTE closed).
- FAIL: controls sluggish. Log: 4 of ~12 runs died at 59 m = first row (~4 s
  warning) → input not registering. Cause + fix in R1-FEEL (100 Hz sampler task).
- Commit: fix(r1): 100 Hz touch sampler task, snappier swipe/lane, input + crash log

### 2026-10-05 — Session 1 (cont.) — R1: full encounter in one round
- User: 3 lanes; build a fully working concept in one development round. Interpreted
  as the complete encounter loop (the part testable without GPS modules / 2nd board).
- Done (R1-R1):
  - lanes.*: per-scanline road written straight into the SRAM band buffer (one loop
    per row, no per-pixel library calls): sky gradient, two parallax hill ridges
    (1024-px sine tables), grass/road stripes on floor((z+travel)/SEG), rumble
    strips, dashed lane lines, sine bend toward the horizon, project() for objects.
    Boot self-test measures band byte order (R1-BYTE).
  - encounter.*: obstacles at fixed world positions (z = wz - travel, so only travel
    moves → trivial interpolation), rows block 1–2 lanes never 3, barrier = jump,
    wall = dodge, speed ramps 50→100 km/h, gap shrinks 30→18 m, 900 m escape goal,
    READY/RUN/WIN/LOSE with retry lockout, painter's-order draw (runner interleaved
    by depth), HUD progress bar + distance + speed + titles. Placeholder art only.
  - input: + laneSwipe edge (re-anchors per 40 px, so one long drag can cross two
    lanes) and pressed (tap) edge.
  - main: R1 loop; serial adds state, distance, speed.
- Host preview (same math in Python) caught a design flaw before flashing: with the
  camera 2 m behind (ROAD_CAM_K 352) obstacles were specks until ~15 m. Moved camera
  to 5 m (K 880) — obstacles readable at 30 m+. Stripe length 4 → 6 m to match.
- Commit: feat(r1): playable encounter — pseudo-3D lanes, runner, obstacles, loop

### 2026-10-05 — Session 1 (cont.) — R0-R2 hw results
- Bars smooth, one touch dot, absolute touch mapping correct (TOUCH-1), heap flat.
- Serial showed 43.1 fps: the Cave Escape frame pacer (render locked to the panel
  refresh period) was never carried over (R0-3). Restored in R0-R3. The unpaced run
  gave a new measurement: full-screen push ≈ 23 ms → ~16 ms headroom per frame.
- CLAUDE.md / PLANNING.md budget lines corrected: 25.3 fps comes from the pacer in
  main.cpp matching the panel refresh, not from the hardware locking frames itself.
- Commit: fix(r0): restore panel-period frame pacer; log R0 hw results

### 2026-10-05 — Session 1 (cont.) — R0-R1 hw test FAILED
- User: bars jerk back and forth; four green dots per touch. Both are R0 harness
  bugs (see R0-1, R0-2), not carried-over modules. Fixed in R0-R2; added touch xy
  to serial to verify absolute mapping. Also corrected docs: bars are vertical,
  not diagonal.
- Commit: fix(r0): bar wrap period + single touch read path; xy debug output

### 2026-10-05 — Session 1 (R0 scaffold)
- Design proposed and accepted: two modes (GPS overworld / pseudo-3D encounters),
  serverless shared world via GPS-cell hashing, ESP-NOW for players.
- Repo created by user; container can READ but not PUSH (Claude GitHub App not
  installed for this repo) → hand-off as tarball + checksum.
- Done: carried over board_config.h (+ GPS pin block), display.*, input.*,
  platformio.ini. renderer.* generalized: scene-agnostic compose callbacks
  (addLayer/clearLayers) so overworld and encounter can swap layer stacks.
  main.cpp = R0 harness (scrolling stripes + touch marker + fps/heap/input stats).
- Compiled in container: SUCCESS. Hardware verify next.
- Commit: chore(r0): scaffold Runners — carried-over display/input/renderer, docs

## Changelog
- v0.5.0 — 2026-10-08 — A1 + A2 pixel art: runner, obstacles, pickups, enemies, HUD and
  shop pictures (hw-verified by user report).
- v0.4.0 — 2026-10-08 — B1 battles (pursuit/hunt/pass), duck control, S1 shop, runner rank,
  audio (SFX + M1 music), volume settings (hw-verified by user report).
- v0.3.0 — 2026-10-08 — R3: overworld + menu + chase mode (hw-verified by user report).
- v0.2.0 — 2026-10-07 — P1: standalone polish (hw-verified by user report).
- v0.1.0 — 2026-10-07 — R1: playable encounter, tap-zone controls (hw-verified R1-R3).
- v0.0.1 — 2026-10-05 — R0 scaffold (hw-verified R0-R2).
