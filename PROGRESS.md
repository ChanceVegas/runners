# PROGRESS.md — Single Source of Truth Across Sessions

Every session: read this first, update it last. If it isn't logged here, the next
session doesn't know it happened.

## Current State
- Phase: P1 polish pass CODE DONE + code-reviewed (tag P1-R2) — compiled; hw-verify
  pending. R1 COMPLETE ✅ (2026-10-07). GPS (R2) + overworld (R3) ON HOLD (user).
- Builds: yes (espressif32@6.5.0; RAM 8.2%, Flash 32.5% of 1.3 MB app partition)
- Runs on hardware: yes — encounter playable, colours correct, controls "much improved"
- Measured (R1-R3): fps 25.2–25.3 paced, render ~21.3 ms (budget 35), heap flat
  281,540 (−4.7 KB vs R1-R1 = touch sampler task stack; stable).

## Next Up (in order)
1. P1 hardware gate (tag P1-R2): banner `=== Runners P1-R2 ===`, then
   `[encounter] best score loaded: 0` on first boot.
   a. Title: big RUNNERS, BEST, blinking TAP TO RUN, two instruction lines and
      "< LANE  JUMP  LANE >" along the bottom — all readable, nothing clipped?
   b. Tap → 3-2-1-GO → run. Coins: ground trails in the open lane, arcs over
      barriers (jump to grab). Score / stage / coins in the top bar.
   c. Crash → red flash, runner knocked flat → CAUGHT! with score, best / NEW BEST!,
      stats line. Power-cycle: the title must show the saved best.
   d. Reach 900 m → ESCAPED! STAGE 1 CLEAR → tap → STAGE 2 countdown, faster.
   e. Pines / bushes / posts streaming past at the roadside.
   f. Serial: fps ≈ 25.3, render ms (new content adds draw calls; budget 35 ms),
      heap flat. Read render ms on the TITLE and GAME OVER screens too, not just
      while running (text-heavy screens; review item 4).
   g. Feel: arc coins over barriers — does grabbing the first coin ever still clip
      the barrier? (Thresholds are both 30 px, so it should only happen on an early,
      already-falling jump; review item 5.)
2. ON HOLD (user, 2026-10-07): R2 GPS, R3 overworld — and the GPS module / 2nd
   board purchases they need. Resume only when the user says so.

## Known Issues / Risks
- TAP-1 (watch): R1-R3 test crash at 236 m — BARRIER in runner's lane, jumpY 0, no
  tap logged after 195 m. Either no jump was attempted or a middle tap was missed.
  If a "tapped but nothing happened" case is ever confirmed, check the one-sample
  bounce discard in input.cpp first (a very light/short tap may read as a bounce).
- CLEAN-1 (fixed P1): "LGFX_USE_V1 redefined" warning — display.h now guards it.
- P1-NVS (accepted): best score is written to flash (Preferences/NVS) inside the
  game loop, but only once per game over, and only on a new best. Can stall a frame
  for a few ms when it happens; invisible on the game-over screen.
- P1-AUDIO (open, not in P1): no sound yet. The board has an I2S speaker amp
  (pins in board_config). Biggest remaining polish item; needs its own pass.
- P1-ART (open): all art is still primitive shapes (rects/ellipses/triangles). Real
  sprites need the pushImage read-back test first (CARRY-1).
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
- CARRY-1 (open, strong lead): Cave Escape showed unexplained magenta on floor tiles.
  R1-BYTE proved band buffers are byte-swapped; Cave Escape's parallax wrote native
  colours straight into them, and its sprite pushImage path never checked byte order.
  R1 uses NO bitmaps. Before the first bitmap art: read-back test of pushImage.
- HW-SUN (open, design): CrowPanel display not sunlight-readable; outdoor play needs
  different field hardware (PLANNING Open Decision #2).
- GPS-INDOOR (open, process): GPS rarely fixes indoors. R2 must ship a fake-GPS
  replay mode so overworld work doesn't require walking outside.
- R4-HW (open): player-to-player testing needs two boards + two GPS units.

## Decisions Made
- 2026-10-05: Cave Escape scrapped by user; archived at tag `cave-escape-final`.
- 2026-10-05: New game "Runners": GPS overworld + pseudo-3D lane encounters.
  Encounters start only when stopped. Deterministic world from GPS cell hash.
- 2026-10-05: Player link = ESP-NOW (no router/server); BLE reserved for phone app.
- 2026-10-05: Prototype on CrowPanel 4.3; field hardware chosen later.
- 2026-10-05: GPS on UART1 header (GPIO18 RX / GPIO17 TX), u-blox M10 recommended.
- Carried from Cave Escape: PlatformIO + Arduino core 2.0.14 (pinned), LovyanGFX,
  band compositor, drag-gesture input, 3-doc process, transfer protocol.

## Session Log (newest first)
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
- v0.2.0 — 2026-10-07 — P1: standalone polish (unverified on hardware).
- v0.1.0 — 2026-10-07 — R1: playable encounter, tap-zone controls (hw-verified R1-R3).
- v0.0.1 — 2026-10-05 — R0 scaffold (hw-verified R0-R2).
