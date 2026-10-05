# PROGRESS.md — Single Source of Truth Across Sessions

Every session: read this first, update it last. If it isn't logged here, the next
session doesn't know it happened.

## Current State
- Phase: R0 CODE DONE (2026-10-05, tag R0-R1) — compiled in container; hw-verify pending.
- Builds: yes (espressif32@6.5.0; RAM 6.6%, Flash 28.8% of 1.3 MB app partition)
- Runs on hardware: pending
- Measured FPS: pending (expect ~25.3 panel-locked; test layer is light)

## Next Up (in order)
1. R0 hardware verify (tag R0-R1): upload [SUCCESS] → RST → banner
   `=== Runners R0-R1 ===` → scrolling diagonal stripes, no band seams or tearing →
   green dot follows finger → serial: fps ≥ 25, render ms, heap flat, moveX responds
   to horizontal drag.
2. R1 — pseudo-3D lane renderer (breakdown for user approval BEFORE code).
3. Purchase: 2× u-blox M10 GPS modules (patch antenna + backup cap), 2nd CrowPanel 4.3.

## Known Issues / Risks
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
