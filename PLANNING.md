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

## Safety (requirement for R2 GPS — raised by user 2026-10-07)
Players move through the real world while the map is FICTIONAL: on-screen water,
rock or forest has no relation to real hazards (a map lake may be a parking lot; a
real river may be map grass). Therefore:
- In-game terrain blocking is a GAMEPLAY choice only and must never be presented as
  a safety feature (it would teach false trust near real water/roads).
- Safety comes from not demanding attention while moving. Proposed for R2:
  1. Safety notice on startup (map is imaginary; real hazards are not shown).
  2. No input ever required while moving (existing rule: chases start only when
     still) — keep absolute.
  3. Audio cue (I2S speaker amp) when an enemy is in range: "stop, then look".
  4. Speed lockout above running pace (~7 m/s): "too fast — paused", nothing counts
     (no play from bikes/cars; also anti-cheat).
  5. Simplified map view while moving (less temptation to stare).
- Future option: real map data (OpenStreetMap) to keep spawns away from real water and
  roads — the only way the game can know real hazards; big step (storage/connectivity).
  Status: items 1–5 APPROVED by user 2026-10-07 — mandatory scope of R2.
  OSM: DEFERRED (user unsure it fits). Note: the full map can't, but a regional
  water/roads extract could live on a microSD card in the board's TF slot (shares the
  touch SPI bus). Revisit only if spawn-placement safety becomes a need.

## Battles, Shop, Art — APPROVED 2026-10-08 (user: "Looks fine. Let's do it.")
User feedback: chases "don't feel like real enemy battles" (enemy never on screen,
one-hit death). User's design: PURSUIT that can turn into a HUNT.

**Battle = Phase 1 Pursuit -> (if caught up) Phase 2 Hunt.** Replaces chase mode.
Arcade mode stays the endless runner.
- Phase 1 — PURSUIT: the enemy is visible BEHIND you (looming at the bottom of the
  screen, bigger as it closes in). Gap meter, start 30 m.
  - Running clean widens the gap slowly (enemy type sets its speed).
  - Hitting an obstacle = STUMBLE (no death): slowed 1 s (barrier) / 1.6 s (wall),
    gap −10 m / −15 m, with camera shake + red border + pursuer lunge (B1-R2).
  - Fewer rows than arcade and walls rare (user, B1-R1: walls felt out of place).
  - Obstacles everywhere (B1-R3): barrier = jump, duck bar = duck, wall = dodge (rare).
    DUCK = tap the bottom of the middle zone.
  - Gap reaches 60 m (or you reach the distance goal) -> ESCAPED (small reward).
  - Gap reaches 0 -> the enemy OVERTAKES you -> Phase 2.
- Phase 2 — HUNT: the enemy is now AHEAD on the road, switching lanes and attacking
  with its own obstacle pattern: Shade drops barriers, Brute smashes in DUCK BARS
  (2 lanes, or all 3 = must duck), Phantom flickers lanes and drops a random obstacle.
  - You have HEARTS (3 base). An attack hit = −1 heart + stumble.
  - Glowing energy ORBS appear in lanes; each one collected strikes the enemy (−1 HP).
    Enemy HP: Shade 4, Brute 6, Phantom 6 (B1-R2).
  - TIME LIMIT (user, B1-R1): fastest possible kill x 1.6. Out of time -> the enemy
    GETS AWAY: no bonus, the battle's coins are lost (CAUGHT also loses them). Scoreboard tracks battles
    won / lost (caught) / got away.
  - Enemy HP 0 -> DEFEATED (big reward + chance of a drink drop). Hearts 0 -> CAUGHT.
- Run-energy shields carry over: each absorbs one stumble's gap loss (Phase 1) or one
  heart (Phase 2).

**Shop** (menu button). Coins from battles + arcade.
- SNEAKERS — permanent stat tiers (3 lines x 3 tiers, e.g. 100 / 250 / 500 coins):
  SPRINT (gap grows faster), SPRING (higher, longer jump), GRIP (shorter stumble,
  smaller gap loss / +1 heart at tier 3).
- ENERGY DRINKS — one-use, carried (max 3 each), offered on a pre-battle screen
  (player is standing still: safe): RUSH (start +15 m gap), GUARD (+1 shield),
  SURGE (orbs strike for 2 in the Hunt).

**Art** — bright chunky pixel art (user: the hardware dictates it).
- Foundation: PNG -> RGB565 + transparency pipeline (host tool), with the pushImage
  read-back self-test (CARRY-1) BEFORE any bitmap ships; a scaled, colour-keyed blit
  that writes the band buffer directly (sprites shrink with depth on the road).
- Content: runner (run cycle, jump, stumble), 3 enemies (front + back views, run
  cycle), obstacles, coin, orb, hearts/HUD icons, shop items; map tiles later.
- Risk: flash use (33.6% of the 1.3 MB app slot now) — repartition if needed.

**Approved order:** B1 battle system (placeholder shapes, gameplay first) -> S1 shop
-> A1 art foundation + runner/obstacles -> A2 enemies + shop/HUD art.

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
| input | src/input.* | touch → abstract actions via TAP ZONES (left/right third = lane step, middle = jump (top) / duck (bottom, B1-R3), any = pressed); 100 Hz sampler task | DONE (R1-R3, hw-verified) |
| lanes | src/lanes.* | pseudo-3D road: sky, hills, 3-lane road, curves, projection, camera shake | DONE (R1, hw-verified: 25.3 fps, ~21 ms) |
| encounter | src/encounter.* | lane-runner engine: runner, obstacles, coins, orbs, stumble, stages, score + saved best (NVS), countdown/crash/clear/game-over flow, HUD; Arcade + Battle modes; engine:: spawn API for battle | P1 hw-verified; Battle mode = B1 |
| battle | src/battle.* | battle rules on top of encounter: Pursuit (gap meter, pursuer behind, hit impact) -> Overtake -> Hunt (enemy ahead attacks per kind, orbs vs HP, hearts, time limit -> GOT AWAY); per-kind data table; placeholder shape art | B1 (awaiting hw) |
| scenery | src/scenery.* | roadside props (pines, bushes, posts) placed by hash of slot index; decoration only | DONE (P1, hw-verified) |
| color | include/color.h | constexpr rgb565() for library draw calls | P1 |
| hud | src/hud.* | shared band-clipped rect/frame/text helpers + fonts | R3 (extracted from encounter) |
| locator | src/locator.* | world position (tile + offset), speed, still/walk/run; SIMULATED from drag now, GPS later behind the same API | DONE (R3, hw-verified) |
| world | src/world.* | deterministic terrain (value noise on integer tiles) + enemy spawns per cell per time window; escaped/revealed memory | DONE (R3, hw-verified) |
| overworld | src/overworld.* | top-down map render (direct band writes), avatar, enemies, engage-when-still, Run energy, map HUD | DONE (R3, hw-verified) |
| game_state | src/game_state.* | mode machine Menu/Explore/Battle/Arcade, layer stacks, battle handshake (shields, Stats, rewards), profile in NVS | DONE (R3, hw-verified); Battle rename B1 |
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
  still, chase mode with Run-energy shields, rewards, saved profile. ✅ DONE
  2026-10-08 (hw-verified by user report).
- **R3 (original plan, superseded):** ON HOLD (user, 2026-10-07). deterministic world from GPS cells, top-down view, avatar
  follows real movement, enemy spawns, encounter trigger on stop.
- **B1 — Battle system:** Pursuit -> Overtake -> Hunt, replaces chase mode
  (placeholder shapes). Built 2026-10-08, awaiting hardware gate.
- **S1 — Shop:** sneakers (stat tiers) + drinks (one-use), pre-battle drink screen. Next.
- **A1 / A2 — Art:** pixel-art pipeline + runner/obstacles, then enemies + shop/HUD.
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
7. **Impassable terrain:** DECIDED 2026-10-07 (user): NO blocking — all map terrain
   stays walkable. The user's underlying concern was real-world safety, which the
   approved Safety section covers; in-game blocking can't protect anyone (fictional
   map) and would desync the avatar from GPS.
8. **Run energy use:** DECIDED R3: shields in chases (50 energy each, max 2, unused
   refunded). The PLANNING "boost" idea is deferred — no free tap zone for it.
6. **Player encounter rules:** what happens when two players meet (race? co-op?). R4.
