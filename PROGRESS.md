# PROGRESS.md — Single Source of Truth Across Sessions

Every session: read this first, update it last. If it isn't logged here, the next
session doesn't know it happened.

## Current State
- Phase: R1 — performance + visuals PASS (R1-R1). Controls: R1-R2 (100 Hz sampler,
  gestures) pushed but not play-tested; superseded by R1-R3 = TAP ZONES (user,
  2026-10-06), compiled; hw-verify pending.
- Builds: yes (espressif32@6.5.0; RAM 7.7%, Flash 29.7% of 1.3 MB app partition)
- Runs on hardware: yes — encounter playable, colours correct ("match beautifully")
- Measured (R1-R1): fps 25.2–25.3 paced, render ~21 ms (title 20.4, running 21.0),
  heap flat 286,204 across ~12 runs. R0-R3 pacer confirmed working.

## Next Up (in order)
1. R1-R3 feel gate (tag R1-R3): banner `=== Runners R1-R3 ===`. Play several runs.
   a. Tap left third = one lane left, right third = one lane right, middle = jump.
      Do taps feel immediate? Any taps ignored or landing in the wrong zone?
   b. Serial logs each tap the game receives (`[in] tap LEFT/RIGHT/MIDDLE ...`) and
      each crash cause (`[hit] ...`). Paste a log covering a few runs.
   c. fps still ~25.3, render ms, heap flat.
2. R2 — GPS bring-up (breakdown for user approval BEFORE code; needs module).
3. Purchase: 2× u-blox M10 GPS modules (patch antenna + backup cap), 2nd CrowPanel 4.3.

## Known Issues / Risks
- R1-BYTE (closed, data): LovyanGFX 16-bit sprite buffers store RGB565 BYTE-SWAPPED
  (boot self-test: red 0xF800 reads 0x00F8). Any direct write into a band buffer
  must swap. lanes::init() detects it and converts; colours verified on hardware.
- R1-FEEL (fix pending hw-verify, R1-R3 tap zones): controls felt sluggish; four deaths at
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
- v0.1.0 — 2026-10-05 — R1 playable encounter (unverified on hardware).
- v0.0.1 — 2026-10-05 — R0 scaffold (hw-verified R0-R2).
