# PLANNING.md — Runners: Design, Architecture & Roadmap

## Game Concept (defined 2026-10-05)
**Real movement fuels the game; your thumb plays it.** Two modes:

**1. Overworld (Pokémon Go aspect).** Top-down fictional map; the player avatar moves
as the real player moves (GPS).
- GPS ground speed classifies the player: still / walking / running.
- Walking explores and reveals the map.
- Running charges **Run energy** and raises the spawn rate of rarer enemies.
- GPS jitter (2–5 m) is irrelevant at map scale.

**2. Encounters (Subway Surfers aspect).** Triggered by meeting an enemy (or player) on
the map. 3rd-person pseudo-3D lane chase: swipe = change lane, flick = jump, Run energy
spends on boosts.
- **Encounters only start once GPS reports the player has stopped.** Safety (no
  lane-dodging while sprinting a trail) and practicality (resistive touch is unusable
  on the move).

**Shared world without a server.** Terrain and enemy spawns are generated
deterministically from a hash of the GPS grid cell (+ a time window for spawns). Every
device builds the SAME world at the same real-world spot — two players standing
together see the same map and the same enemy, no internet involved.

**Players meet over ESP-NOW** (Espressif peer-to-peer on the Wi-Fi radio: no router,
no pairing, ~100–200 m line-of-sight, low latency). BLE kept in reserve for a phone
companion app. Accepted limit: you only meet players who are physically nearby.

Explicitly OUT of v1: global/online multiplayer, cellular, servers, audio, real-map
data (OpenStreetMap etc. — the map is fictional by design).

## Rendering Approach
- Carried over: dual-core band compositor (SRAM bands → PSRAM framebuffer), now
  scene-agnostic via registered compose callbacks (renderer::addLayer).
- Overworld: top-down tile map, scrolled by camera.
- Encounter: pseudo-3D lane road per the reference article (per-scanline projection,
  sprites scaled by depth), Pole Position / OutRun class — NOT Subway Surfers 3D.
- Art lives in flash. Default partition gives a 1.3 MB app slot; a custom
  partitions.csv can fund ~2.5 MB+ of assets when art demands it (Doom-on-CYD approach).

## Module Map (update as built)
| Module | Files | Purpose | Status |
|---|---|---|---|
| board_config | include/board_config.h | pins, panel timings, GPS UART | carried over (hw-verified) |
| config | include/config.h | all tunables | R0 |
| main | src/main.cpp | init + fixed-timestep loop + stats | R0 test harness |
| display | src/display.* | panel + touch driver init (LovyanGFX) | carried over (hw-verified) |
| renderer | src/renderer.* | band compositor, layer callbacks | R0 (generalized from Cave Escape) |
| input | src/input.* | touch gestures → abstract actions | carried over; R1 remaps to lanes |
| lanes | src/lanes.* | pseudo-3D encounter renderer | R1 |
| gps | src/gps.* | UART NMEA parse, fix/speed, fake-GPS replay | R2 |
| world | src/world.* | GPS cell → deterministic map + spawns | R3 |
| overworld | src/overworld.* | top-down map view + avatar | R3 |
| link | src/link.* | ESP-NOW presence + encounter handshake | R4 |
| game_state | src/game_state.* | overworld / encounter / menus | R5 |

## Performance Budget
- Frame = 39.6 ms (panel-locked ~25.3 fps). Cave Escape compose ran 34–36 ms with
  parallax + sprites; budget for the lane renderer is ≤ 35 ms compose.
- Draw cost ≈ call count × ~3 µs. Design renderers around few, large calls.
- Logic runs on core 1 alongside compose; GPS parsing and ESP-NOW must stay light
  (callbacks enqueue, game loop consumes).

## Roadmap (risk-first)
- **R0 — Toolchain + scaffold:** new repo, carried-over display/input/renderer, test
  pattern, fps stats. Gate: builds, flashes, banner, fps ≥ 25, touch marker tracks finger.
- **R1 — Lane renderer (GO/NO-GO):** pseudo-3D road with 3 lanes, scrolling, a player
  sprite and obstacles scaled by depth. Gate: ≥ 25 fps. If it fails, the encounter
  design is revisited before anything else is built on it.
- **R2 — GPS bring-up:** M10 on UART1, NMEA parse, fix/speed/still-walking-running
  classification, FAKE-GPS replay mode for desk development. Gate: outdoor walk test.
- **R3 — Overworld:** deterministic world from GPS cells, top-down view, avatar
  follows real movement, enemy spawns, encounter trigger on stop.
- **R4 — Players:** ESP-NOW presence between two boards, shared-world check (both see
  the same enemy at the same spot), player-vs-player encounter handshake.
- **R5 — Game loop:** states, Run energy economy, scoring/progression, HUD, menus.
- **R6 — Field hardware + polish:** sunlight-readable display, battery, enclosure,
  real art pass, power management.

## Open Decisions
1. **GPS module:** recommended u-blox M10 breakout with patch antenna + backup
   battery/supercap (fast re-fix). Two units. User to purchase.
2. **Field hardware:** CrowPanel 4.3 is prototype-only (transmissive TFT washes out
   in sun, resistive touch, no onboard battery). Decide by R6.
3. **Encounter controls:** swipe/flick on resistive single-touch; validate at R1.
4. **Map scale + cell size:** how many real meters per map tile / per world cell. R3.
5. **Spawn time window:** how often the shared spawn table rolls (e.g. 5–15 min). R3.
6. **Player encounter rules:** what happens when two players meet (race? co-op?). R4.
