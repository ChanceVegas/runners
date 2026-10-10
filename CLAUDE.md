# CLAUDE.md — Runners

Read this file, PLANNING.md and PROGRESS.md at the start of EVERY session, from a
fresh clone of the repo. The repo is the only source of truth; chat attachments and
memory summaries are secondary. If anything contradicts the repo, stop and raise it.

## Project
Runners: GPS-driven outdoor game. Real-world walking/running moves the player across a
top-down overworld map (GPS exploration); encounters with enemies and nearby players play
out as 3rd-person pseudo-3D lane chases (3-lane chase). Design in PLANNING.md.
Originality rules (PLANNING.md) are binding: check every new feature against them, and
never name or imitate other games in code, art, text or docs.

Predecessor: Cave Escape (repo ChanceVegas/cave-escape, archived at tag
`cave-escape-final`). Renderer, input, toolchain and process carried over from it.

Technique reference (pseudo-3D on this exact chip):
https://medium.com/@davidmonterocrespo24/how-i-built-the-first-3d-racing-game-for-esp32-s3-because-someone-said-ai-could-do-it-better-50236a02286f
Its projection math is now DIRECTLY relevant (encounter lanes = pseudo-3D road).

## Hardware — CONFIRMED (esptool readout 2026-10-05)
Elecrow CrowPanel 4.3" Basic (ESP32-S3-WROOM-1-N4R2). Prototype platform; field hardware
is an open decision (PLANNING).
- ESP32-S3 dual-core LX7 @ 240 MHz, 512 KB SRAM, Wi-Fi + BLE 5
- Flash 4 MB QIO. PSRAM 2 MB QUAD SPI (not octal) — bandwidth is the scarce resource
- Display 480x272 TFT, 16-bit RGB parallel, NV3047. No panel GRAM: the S3 DMAs the
  PSRAM framebuffer to the panel continuously.
- Touch: XPT2046 resistive, SINGLE point, on SPI shared with TF slot
- UART1 header (GPIO18 RX / GPIO17 TX) = reserved for GPS
- Two boards needed from R4 (player-to-player); user is acquiring a second.

Rendering rules (measured on this board, binding):
- Compose in internal-SRAM bands, push to PSRAM framebuffer. Never draw per-pixel
  into the PSRAM framebuffer. Never put art in PSRAM — art lives in flash.
- The panel refreshes at 25.26 Hz (39.59 ms, from the RGB timings). main.cpp paces
  render to that period; rendering faster wastes CPU and tears. That is the budget.
- Measured (R0, 2026-10-05): a full-screen push costs ~23 ms on core 0, running in
  parallel with compose on core 1. Frame time ≈ max(compose, push), so compose can
  use up to ~39 ms before frames drop.
- Draw cost is dominated by CALL COUNT, not pixels: ~3 µs per LovyanGFX call.
  Prefer few large calls (block pushImage, fillRect) over many small ones.
- No band.drawPixel() loops in hot paths.
- Drawn geometry must never exceed its collision box (what you see = what you hit).

## Toolchain
PlatformIO, Arduino framework (espressif32@6.5.0 = core 2.0.14, pinned), C++, LovyanGFX.
- Build `pio run` · Flash `pio run -t upload` · Serial `pio device monitor -b 115200`

## Coding Rules
1. Modular: one system per .h/.cpp pair. No god files.
2. No dynamic allocation (`new`/`malloc`/`String`) in the game loop. Allocate at init.
3. Colors RGB565. Art compiled in as const arrays (flash).
4. Fixed-timestep update decoupled from render. Measure fps, don't assume.
5. Tunables ONLY in include/config.h, each with units + intent comment.
6. float OK (S3 has FPU); never double.
7. Every module: one-line purpose comment at top + listed in PLANNING.md module map
   (update PLANNING in the same commit that adds a module).
8. Hardware test protocol: every flashable build carries a banner tag
   (`=== Runners <TAG> ===`). Upload must end `[SUCCESS]`, press RST, confirm the
   banner BEFORE testing anything. Wrong banner = stale flash; stop.

## Session Workflow (mandatory)
1. Clone fresh; read CLAUDE.md + PLANNING.md + PROGRESS.md before writing code.
2. Work ONE task from PROGRESS "Next Up". Small scope.
3. Larger milestones get a breakdown approved by the user BEFORE code.
4. Nothing advances past a hardware gate without user-reported results.
5. Before ending: update PROGRESS.md (state, session log, known issues).
6. Commits: `type(scope): summary` (feat, fix, refactor, docs, perf, chore, diag).
   One logical change per commit. Never commit non-compiling code.
7. Design problems are logged in PROGRESS "Known Issues" and raised — never silently
   worked around.

## Transfer Protocol (direct push — enabled 2026-10-07)
The Claude GitHub App has write access to this repo, so Claude pushes directly.
- Claude commits and pushes to `main` itself. Before pushing: `git fetch origin main`
  and rebase onto it, and `pio run` must succeed (never push non-compiling code).
- One logical change per commit, `type(scope): summary` (Session Workflow rule 6).
- The user's side: `git pull` before every flash, then confirm the banner tag.
  No more patches, `git am`, or checksums.
- Hardware gates still need the user: Claude cannot reach the board. Nothing is
  marked hw-verified without user-reported results (Session Workflow rule 4).
- If a push is ever refused (403), fall back to the old way: one `git format-patch`
  per hand-off, user applies with `git am` and pastes `git log --oneline -1`.

## Repo Map
- CLAUDE.md — rules, hardware, toolchain, protocols
- PLANNING.md — design, architecture, module map, roadmap, open decisions
- PROGRESS.md — current state, next up, known issues, session log, changelog
- src/, include/, tools/ — code; tools/ = host-side generators
