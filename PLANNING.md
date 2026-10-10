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

Explicitly OUT of v1: global/online multiplayer, cellular, servers, real-map
data (OpenStreetMap etc. — the map is fictional by design). Sound effects and chiptune music ARE in
(speaker fitted 2026-10-08; also needed for Safety item 3).

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
  - CLOSING IN (RK-R2, user: "can never catch the enemy"): the enemy starts 30 m
    ahead and every orb hit pulls it closer (11 m at 1 HP). HP 0 = "CAUGHT IT!" — you
    run past it, then DEFEATED. Attacks never land closer than 18 m. Orb pity: never
    two attacks in a row without an orb.
  - PASS (RK-R3, user): after "CAUGHT IT!" you run past it — "YOU PASSED THE <NAME>!".
    DEFEATED is locked in (bonus, drop, defeat XP) and the race restarts: gap 25 m, the
    enemy weakened (+0.2 m/s gap gain), a new finish 300 m on. Escape = DEFEATED
    screen. If it catches you again: a Hunt at half HP; losing then still loses the
    battle's coins but keeps the defeat bonus.
  - TIME LIMIT (user, B1-R1): fastest possible kill x 2.4 (was 1.6 until RK-R2). Out of time -> the enemy
    GETS AWAY: no bonus, the battle's coins are lost (CAUGHT also loses them). Scoreboard tracks battles
    won / lost (caught) / got away.
  - Enemy HP 0 -> DEFEATED (big reward + chance of a drink drop). Hearts 0 -> CAUGHT.
- Run-energy shields carry over: each absorbs one stumble's gap loss (Phase 1) or one
  heart (Phase 2).

**Runner level (L1 — APPROVED 2026-10-09, replaces rank).**
- XP from every battle, never lost (user: losses don't cost XP or levels). Base XP:
  escape 10, defeat 25, got away 5, caught 3; x1 / 1.5 / 2 by enemy level, x1.0 /
  1.2 / 1.4 Shade / Brute / Phantom. Levels 1–50 (user); XP from L to L+1 =
  40 + 10 x L (L2 at 50 XP, ~14,200 total to L50). Saved as "xp" (old "rankpts"
  migrated at 10 XP per point).
- Difficulty d = (level − 1) x 9 / 49, so L50 = the old rank 10. Per d: −0.04 m/s
  Pursuit gap gain (floor 0.15), +5% gap lost per stumble, +0.5 m/s road speed, rows
  3% denser (floor 0.7x), +1 HP per 2 d, attacks 4% more often (floor 0.65x), 2% fewer
  orbs (floor 35%), win bonuses +15%. SPRINT III is how you keep up late.
- Start position (L1b): L1–14 enemy behind (Pursuit), L15–29 beside, L30–50 ahead =
  straight into the Hunt. Escape rules at beside/ahead: OPEN (decide with tuning).
- Display (L1c): XP bar + level on the map top bar; "+XP / LEVEL UP!" on end screens.
  Level-up rewards: user wants them, WHAT is OPEN — L1c leaves a hook.
- Steps: L1a XP + levels + scaling (banner L1A) -> L1b start positions -> L1c display.

**Swimming (user 2026-10-09, look only):** on water tiles the map avatar swims (own sprites,
wake); no gameplay effect (movement is real walking).

**E1 — three new enemies (APPROVED 2026-10-09, user: "everything looks fine. i approve").**
| Enemy | Unlock | Hunt attack | Pursuit | HP | XP |
|---|---|---|---|---|---|
| STALKER | L10 | moves into YOUR lane, blocks it + one neighbour | closes the gap faster | 6 | x1.6 |
| HORNET | L20 | combos: low barrier then duck bar right after (jump, then duck); attacks often | denser rows | 5 | x1.8 |
| WARDEN | L35 | walls across two lanes; slow but heavy; boss | 1,200 m race | 10 | x2.5 |
- Shared world: new kinds spawn for EVERYONE (deterministic hash, so players at one spot
  see the same enemy); below the unlock level they show grey with a padlock and can't be
  fought. Spawn mix (common): Shade 48 / Brute 24 / Stalker 12 / Hornet 10 / Warden 6 %.
- Steps: E1a roster + locked display -> E1b Stalker -> E1c Hornet -> E1d Warden (each:
  behaviour + art + hw test; DEBUG_UNLOCK_ALL to test early).
  User (2026-10-09): build all of them in one go. Warden hit = 2 hearts; Hornet combo
  spacing = speed x 0.95 s.
- Drinks unchanged: each is used up by the one battle it's picked for (user).

**Shop** (menu button). Coins from battles + arcade.
- SNEAKERS — permanent stat tiers (3 lines x 3 tiers, e.g. 100 / 250 / 500 coins):
  SPRINT (gap grows faster), SPRING (higher, longer jump), GRIP (shorter stumble,
  smaller gap loss / +1 heart at tier 3).
- ENERGY DRINKS — one-use, carried (max 3 each), offered on a pre-battle screen
  (player is standing still: safe): RUSH (start +15 m gap), GUARD (+1 shield),
  SURGE (orbs strike for 2 in the Hunt).
- S1 numbers (config.h): tiers 100/250/500; SPRINT +0.12/0.24/0.36 m/s gap gain;
  SPRING jump x1.06/1.12/1.18; GRIP x0.85/0.70/0.60 stumble time + gap loss, +1 heart
  at III. Drinks RUSH 50 / GUARD 60 / SURGE 80, max 3 each; 25% drop on DEFEATED.
  Sneakers are battle-only (arcade stays fair for the best score). Arcade banks
  ARCADE_WALLET_PCT (50%) of its coins at game over (user: "yes, but reduced").

**Art** — bright chunky pixel art (user: the hardware dictates it).
- Foundation (A1, built 2026-10-08): text-grid pixel art -> palette-indexed sprites
  (tools/art_gen.py; PNG import can be added if the user draws art), and a scaled,
  transparent-index blit (sprite.*; no pushImage, so CARRY-1 can't bite) 
  that writes the band buffer directly (sprites shrink with depth on the road).
- A1 DONE (runner, obstacles, coin, orb). A2 DONE 2026-10-08 (enemies front/back, down
  pose, HUD icons, shop pictures; hw-verified). A3 built 2026-10-08: map tiles, avatar, map enemies.
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
| battle | src/battle.* | battle rules on top of encounter (+ runner level/XP scaling and helpers): Pursuit (gap meter, pursuer behind, hit impact) -> Overtake -> Hunt (enemy ahead attacks per kind, orbs vs HP, hearts, time limit -> GOT AWAY); per-kind data table; placeholder shape art | DONE (B1..RK-R3, hw-verified) |
| audio | src/audio.* | square/noise synth on the onboard I2S amp, own task (core 0): 2 SFX voices + M1 music sequencer (lead/bass/drums); play(Sfx), music(Track); volumes in config | AU0/M1/VOL hw-verified 2026-10-08 |
| sprite | src/sprite.* | palette-indexed sprite blitter: direct band-buffer writes, scaled, flip, colour 0 transparent | A1 |
| art_data | src/art_data.h | GENERATED sprite pixels + shared palette (flash) — edit tools/art_gen.py | A1 |
| art_gen | tools/art_gen.py | host tool: text-grid / generated pixel art -> src/art_data.h (+ --preview PNG) | A1 |
| music_data | src/music_data.h | GENERATED song row tables (flash) — edit tools/music_gen.py, not this file | M1 |
| music_gen | tools/music_gen.py | host tool: song notation -> src/music_data.h | M1 |
| settings | src/settings.* | SETTINGS screen (Music / Sound levels 0-10, mute), NVS vmus/vsfx/mute, in-run mute corner icon | DONE (VOL-R1, hw-verified) |
| shop | src/shop.* | gear: sneaker tiers + drinks inventory (NVS), SHOP screen, PRE-BATTLE drink screen, gear -> battle::Stats, defeat drink drop | DONE (S1, hw-verified) |
| scenery | src/scenery.* | roadside props (pines, bushes, posts) placed by hash of slot index; decoration only | DONE (P1, hw-verified) |
| color | include/color.h | constexpr rgb565() for library draw calls | P1 |
| hud | src/hud.* | shared band-clipped rect/frame/text helpers + fonts | R3 (extracted from encounter) |
| locator | src/locator.* | world position (tile + offset), speed, still/walk/run; SIMULATED from drag now, GPS later behind the same API | DONE (R3, hw-verified) |
| world | src/world.* | deterministic terrain (value noise on integer tiles) + enemy spawns per cell per time window; escaped/revealed memory | DONE (R3, hw-verified) |
| overworld | src/overworld.* | top-down map render (pixel-art tile rows, direct band writes; A3), avatar sprite, enemies, engage-when-still, Run energy, map HUD | DONE (R3, hw-verified) |
| game_state | src/game_state.* | mode machine Menu/Explore/PreBattle/Battle/Arcade/Shop/Settings, layer stacks, battle handshake (shields, Stats, rewards), profile in NVS | DONE (R3, hw-verified); Battle rename B1 |
| gps | src/gps.* | UART1 NMEA (GGA/RMC/GSV), baud autodetect, fix/sats/position, HUD status; feeds locator in R2 | G0 bring-up (awaiting hw) |
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
  (placeholder shapes). DONE 2026-10-08 (hw-verified, incl. rank + pass).
- **S1 — Shop:** sneakers (stat tiers) + drinks (one-use), pre-battle drink screen.
  DONE 2026-10-08 (hw-verified). Arcade banks 50% of its coins (user).
- **L1 — Levels + XP:** replaces rank; level-driven start positions. APPROVED
  2026-10-09. DONE (L1a-c hw-verified).
- **E1 — 3 new enemies:** unlocked by player level. APPROVED 2026-10-09; all built (one test build).
- **M1 — Music:** 4 upbeat chiptune loops (menu, explore, battle, arcade) under the
  SFX (user, 2026-10-08). DONE 2026-10-08 (hw-verified).
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
9. **OpenStreetMap world (user, 2026-10-10: "I want to use OpenStreetMap. Going to have
   to figure that out."):** OPEN, future. Idea: drive the map from real OSM data
   (water = real water, forest = parks/woods, trail = footpaths, roads) instead of the
   pure hash terrain. Points to settle first:
   - No internet in the field: pre-processed data for a play area, stored on the TF card
     (slot is on the board; 4 MB flash is too small). A host tool (tools/) converts an OSM
     extract into our tile format. Coverage size vs card space TBD.
   - Shared world stays deterministic only if every device has the same extract version.
   - Licence: ODbL — "© OpenStreetMap contributors" must be shown in-game. Public tile
     servers forbid bulk downloading; use extracts (e.g. Geofabrik), not their tiles.
   - Safety bonus: real roads/water are known, so spawns can avoid roads and real water
     (ties into the Safety section).
   - Needs a working GPS first (G0 / PWR-1).
