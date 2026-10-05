# PROGRESS.md — Single Source of Truth Across Sessions

Every session: read this first, update it last. If it isn't logged here, the next
session doesn't know it happened.

## Current State
- Phase: R0 HW-VERIFIED on R0-R2 (2026-10-05) except frame pacing; R0-R3 restores
  the pacer — final check pending (fps must read ~25.3).
- Builds: yes (espressif32@6.5.0; RAM 6.6%, Flash 28.8% of 1.3 MB app partition)
- Runs on hardware: yes — scrolling bars smooth, one touch dot, heap flat (~289.6 KB)
- Measured: unpaced 43.1 fps, render 22–24 ms = full-screen push cost (R0-3).

## Next Up (in order)
1. R0-R3 pacing check: banner `=== Runners R0-R3 ===`, serial fps ≈ 25.3 (not 43),
   bars still smooth. Then R0 is closed.
2. R1 — pseudo-3D lane renderer (breakdown for user approval BEFORE code).
3. Purchase: 2× u-blox M10 GPS modules (patch antenna + backup cap), 2nd CrowPanel 4.3.

## Known Issues / Risks
- R0-3 (fix pending hw check, R0-R3): frame pacer from Cave Escape main.cpp was not
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
- CARRY-1 (open): Cave Escape's sprite color-key path showed unexplained magenta on
  floor tiles (ART-2, diagnostic never run — project pivoted). Sprite code is NOT
  carried into R0. When R1 adds sprites, run a pushImage round-trip self-test
  (read back pixels after push) before trusting any art on screen.
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
- v0.0.1 — 2026-10-05 — R0 scaffold (unverified on hardware).
