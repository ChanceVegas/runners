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
| renderer | src/renderer.* | band compositor, layer callbacks, band byte-order test + raw() colour conversion | DONE (R0); raw() moved here R3 |
| input | src/input.* | touch → abstract actions via TAP ZONES (left/right third = lane step, middle = jump, any = pressed); 100 Hz sampler task | DONE (R1-R3, hw-verified) |
| lanes | src/lanes.* | pseudo-3D road: sky, hills, 3-lane road, curves, projection | DONE (R1, hw-verified: 25.3 fps, ~21 ms) |
| encounter | src/encounter.* | lane-chase game: runner, obstacles, coins, stages, score + saved best (NVS), countdown/crash/clear/game-over flow, HUD; Arcade + Chase modes | P1 hw-verified; chase mode = R3 |
| scenery | src/scenery.* | roadside props (pines, bushes, posts) placed by hash of slot index; decoration only | DONE (P1, hw-verified) |
| color | include/color.h | constexpr rgb565() for library draw calls | P1 |
| hud | src/hud.* | shared band-clipped rect/frame/text helpers + fonts | R3 (extracted from encounter) |
| locator | src/locator.* | world position (tile + offset), speed, still/walk/run; SIMULATED from drag now, GPS later behind the same API | R3 code done, hw-verify pending |
| world | src/world.* | deterministic terrain (value noise on integer tiles) + enemy spawns per cell per time window; escaped/revealed memory | R3 code done, hw-verify pending |
| overworld | src/overworld.* | top-down map render (direct band writes), avatar, enemies, engage-when-still, Run energy, map HUD | R3 code done, hw-verify pending |
| game_state | src/game_state.* | mode machine Menu/Explore/Chase/Arcade, layer stacks, chase handshake, profile in NVS | R3 code done, hw-verify pending |
| gps | src/gps.* | UART NMEA parse, fix/speed; feeds locator | R2 (on hold) |
| link | src/link.* | ESP-NOW presence + encounter handshake | R4 |

## Performance Budget
- Frame = 39.59 ms (render paced to the 25.26 Hz panel refresh). Full-screen push
  measured at ~23 ms (R0, unpaced run at 43 fps); push runs on core 0 in parallel
  with compose on core 1. Budget for the lane renderer: compose ≤ 35 ms.
- Draw cost ≈ call count × ~3 µs. Design renderers around few, large calls.
- Logic runs on core 1 alongside compose; GPS parsing and ESP-NOW must stay light
  (callbacks enqueue, game loop consumes).

## Roadmap (risk-first)
- **R0 — Toolchain + scaffold:** new repo, carried-over display/input/renderer, test
  pattern, fps stats. Gate: builds, flashes, banner, fps ≥ 25, touch marker tracks finger.
- **R1 — Encounter (GO/NO-GO):** ✅ DONE 2026-10-07 (GO). Built in ONE round per user (2026-10-05): pseudo-3D
  3-lane road with curves + hills, runner, barrier (jump) / wall (dodge) rows, swipe +
  flick controls, collision, 900 m escape goal, READY → RUN → WIN/LOSE → retry loop.
  Gate: ~25.3 fps with compose ≤ 35 ms, AND controls feel playable on resistive touch.
  If either fails, the encounter design is revisited before anything else builds on it.
- **P1 — Standalone polish pass (user, 2026-10-06):** GPS + overworld ON HOLD by user.
  "As close to a polished game as possible minus GPS, in one pass": smooth FreeSans
  fonts (bigger instructions, requested), coins + score, saved best score, stages,
  roadside scenery, 3-2-1 countdown, crash flash + game-over screen, runner lean.
- **R2 — GPS bring-up (ON HOLD):** ON HOLD (user, 2026-10-07). M10 on UART1, NMEA parse, fix/speed/still-walking-running
  classification, FAKE-GPS replay mode for desk development. Gate: outdoor walk test.
- **R3 — Overworld:** built in ONE iteration per user (2026-10-07), BEFORE R2 GPS:
  position is simulated from drag input behind the locator API, so R2 only adds a
  GPS reader. Menu (EXPLORE / ARCADE), deterministic terrain + spawns, engage when
  still, chase mode with Run-energy shields, rewards, saved profile. Code done;
  hw-verify pending (tag R3-R1).
- **R3 (original plan, superseded):** ON HOLD (user, 2026-10-07). deterministic world from GPS cells, top-down view, avatar
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
3. **Encounter controls:** DECIDED 2026-10-06 (user): TAP ZONES — left third = lane
   left, right third = lane right, middle = jump. Swipe/flick gestures were removed:
   gesture recognition needs several samples, so it lagged on resistive touch even at
   100 Hz; a zone tap fires on the 2nd sample. Lanes DECIDED: 3 (user, 2026-10-05).
4. **Map scale + cell size:** DECIDED R3 (tunable): 6 m per tile, 60 m spawn cells,
   ~1 enemy per screen. Revisit with real walking at R2.
5. **Spawn time window:** DECIDED R3 (tunable): 10 min. Sim uses a boot-relative
   clock; R2 must switch to GPS UTC so devices share windows.
7. **Impassable terrain:** REOPENED 2026-10-07 (user): the player should NOT pass
   through dense woods, rock or water — to be fixed with GPS (R2). Was: "water
   walkable" (R3). Conflict: with GPS the avatar tracks a real person and the map
   is fictional. Options proposed: (1) tethered avatar — stops at blocked terrain,
   faint marker shows true GPS position, rejoins when the real position is passable;
   (2) blocked terrain = dead ground (crossable, but no energy/distance, no engaging);
   (3) hard-block in sim, (1)/(2) with GPS. Recommended: (1) for water + rock, and
   split forest into walkable forest + impassable "thicket". AWAITING user choice.
8. **Run energy use:** DECIDED R3: shields in chases (50 energy each, max 2, unused
   refunded). The PLANNING "boost" idea is deferred — no free tap zone for it.
6. **Player encounter rules:** what happens when two players meet (race? co-op?). R4.
