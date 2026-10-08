// config.h — ALL tunable constants live here (CLAUDE.md rule 5).
// Every constant states units and the feel/budget intent behind it.
#pragma once

// --- Frame timing ---
#define TARGET_FPS          25      // fps floor. Carried from Cave Escape: the RGB panel
                                    // refresh phase-locks frames at ~25.3 fps (39.6 ms).
#define UPDATE_HZ           60      // Hz; fixed-timestep logic, decoupled from render

// --- Band compositor ---
#define BAND_HEIGHT         34      // px; 272/34 = 8 bands, 480*34*2 = 32,640 B SRAM each
#define RENDER_MAX_LAYERS   8       // max compose callbacks per frame

// --- Input: single-touch gestures, sampled by a dedicated task (R1-R2) ---
#define INPUT_SAMPLE_HZ      100    // touch reads per second. The game loop alone only
                                    // sampled ~25 Hz (ticks run in a burst per frame),
                                    // which made controls feel sluggish (R1-FEEL).
#define INPUT_DEADZONE_PX    10     // px of drag before moveX registers (UI use; not encounter)
#define INPUT_JOY_RANGE_PX   60     // px of drag = full deflection
#define INPUT_EMA_ALPHA      0.6f   // x/y smoothing per sample 0..1 (drag axis/UI point)
#define INPUT_ZONE_LEFT_X    160    // px; taps left of this = lane left (left third)
#define INPUT_ZONE_RIGHT_X   320    // px; taps at/right of this = lane right; between = jump

// --- Encounter road: pseudo-3D projection (R1) ---
// Screen row y below the horizon has d = y - HORIZON rows of depth; world depth
// z = ROAD_CAM_K / d metres and on-screen scale s = d / ROAD_ROWS. Bottom row
// (d = 176) sits at z = 5 m, s = 1; the runner's feet row is at z ≈ 5.7 m.
#define ROAD_HORIZON_Y       96     // px; horizon row. Lower = more sky, flatter road
#define ROAD_ROWS            (LCD_HEIGHT - ROAD_HORIZON_Y)   // 176 road rows
#define ROAD_CAM_K           880.0f // m*rows; z = K/d. Camera distance behind the runner.
                                    // Host preview: at 352 (camera 2 m back) obstacles
                                    // were specks until ~15 m away (~0.6 s to react at
                                    // top speed); 880 makes them readable at 30 m+
#define ROAD_HALF_PX         300.0f // px; road half-width at the bottom row (> 240 so
                                    // the road fills the bottom of the screen)
#define ROAD_SEG_M           6.0f   // m per alternating stripe segment; sells speed
#define ROAD_CURVE_PX        140.0f // px; max sideways bend at the horizon
#define ROAD_CURVE_PERIOD_M  220.0f // m; distance over which the bend sweeps L-R-L
#define ROAD_BG_PARALLAX     0.02f  // near-hill drift, px per (px of bend x m run):
                                    // full bend at 20 m/s drifts hills ~56 px/s

// --- Encounter runner (R1) ---
#define RUN_FEET_Y           250    // px; screen row of the runner's feet
#define RUN_LANE_SPEED       10.0f  // lanes/s sideways; 100 ms per lane change (was 7)
#define DEBUG_INPUT_LOG      1      // 1 = print each tap the encounter consumes + crash causes

// --- Game flow / polish (R1 polish pass) ---
#define RUN_COUNTDOWN_S      2.0f   // s of "3-2-1-GO" before a stage starts
#define RUN_CRASH_S          0.9f   // s of crash freeze + flash before GAME OVER
#define RUN_CRASH_FLASH_S    0.18f  // s the red screen border flashes on impact
#define RUN_HINT_S           5.0f   // s the "<  JUMP  >" tap hints stay up into a run
#define RUN_FIRST_ROW_M      80.0f  // m ahead of the start where the first row sits
                                    // (R1 had 66 m: ~4 s, fine; a bit more room now
                                    // that the countdown ends with the runner moving)
// Stages: escaping (RUN_GOAL_M) advances the stage; each stage is faster and denser.
#define STAGE_SPEED_STEP     3.0f   // m/s added to start and max speed per stage
#define STAGE_SPEED_CAP      38.0f  // m/s absolute cap (~137 km/h)
#define STAGE_WALL_STEP      5      // % more walls per stage
#define STAGE_WALL_CAP       70     // % max wall share

// --- Coins (R1 polish pass) ---
#define COIN_POOL            40     // max live coins
#define COIN_POINTS          10     // score per coin (1 m run = 1 point)
#define COIN_TRAIL_CHANCE    60     // % of obstacle rows that get a coin trail
#define COIN_TRAIL_N         5      // coins in a ground trail (in the open lane)
#define COIN_TRAIL_STEP_M    3.0f   // m between trail coins
#define COIN_TRAIL_MARGIN_M  6.0f   // m past the previous row before a trail may start
#define COIN_ARC_CHANCE      40     // % of trails that are an arc over a barrier instead
#define COIN_R_PX            22.0f  // px coin radius at scale 1
#define COIN_LOW_PX          30.0f  // px coin centre height above road, ground coins
#define COIN_HIGH_PX         95.0f  // px coin centre height, arc coins (need a jump)
#define COIN_HIT_DEPTH_M     1.0f   // m collect window either side of the runner
#define COIN_HIGH_MIN_JUMP   30.0f  // px of jump needed to grab an arc coin
#define COIN_LOW_MAX_JUMP    55.0f  // px; jumping higher than this sails over ground coins

// --- Roadside scenery (R1 polish pass) ---
#define SCENERY_SPACING_M    11.0f  // m between props on one side (sides are offset by half)
#define SCENERY_FAR_M        100.0f // m; props beyond this aren't drawn (specks anyway)
#define SCENERY_LANE_OFFSET  2.3f   // lanes from centre; road edge is 1.5, so off the road
#define SCENERY_LANE_JITTER  1.2f   // lanes of random extra offset per prop
#define RUN_JUMP_VEL_PX_S    520.0f // px/s launch; apex = v^2/2g ≈ 90 px
#define RUN_GRAVITY_PX_S2    1500.0f// px/s^2; airtime = 2v/g ≈ 0.69 s
#define RUN_SPEED_START      14.0f  // m/s at the start of a run
#define RUN_SPEED_MAX        28.0f  // m/s cap; reaction window shrinks as speed climbs
#define RUN_SPEED_RAMP       0.6f   // m/s gained per second of running
#define RUN_GOAL_M           900.0f // m to escape the pursuer = win (~45 s)
#define RUN_ATTRACT_SPEED    6.0f   // m/s road scroll on the title screen
#define RUN_END_LOCKOUT_S    0.8f   // s after win/lose before a tap restarts

// --- Encounter obstacles (R1) ---
#define OBST_POOL            24     // max live obstacles (rows of 1–2, ~5 rows on road)
#define OBST_SPAWN_Z         110.0f // m ahead where new rows appear (near the horizon)
#define OBST_GAP_START_M     30.0f  // m between rows at the start
#define OBST_GAP_MIN_M       18.0f  // m between rows at full difficulty (0.64 s at max
                                    // speed; a lane change takes 0.14 s)
#define OBST_WALL_CHANCE     45     // % of blocks that are walls (must dodge sideways)
#define OBST_HIT_DEPTH_M     0.7f   // m; collision window either side of the runner.
                                    // > half the per-tick travel (0.47 m at 28 m/s),
                                    // so no obstacle can skip through the window
#define OBST_HIT_LANE        0.45f  // lanes; sideways overlap that counts as a hit
#define OBST_W_PX            120.0f // px; obstacle width at scale 1 (lane = 200 px)
#define OBST_BARRIER_H_PX    50.0f  // px at scale 1; low, jumpable
#define OBST_WALL_H_PX       170.0f // px at scale 1; tall, unjumpable
#define OBST_CLEAR_PX        30.0f  // px of jump height needed to clear a barrier

// --- Colors (RGB565) ---
#define COLOR_BG_DEBUG       0x0000 // black
#define COLOR_TOUCH_DEBUG    0x07E0 // green — touch-point marker (debug)

// ============================================================================
// --- Overworld (R3) ---
// World coordinates are metres east (x) / south (y) on an absolute grid. The map is
// generated from hashes of grid indices, so the same spot always looks the same and
// (once GPS lands) every device builds the same world there.
#define OW_TILE_M            6.0f   // m per map tile. Smaller = more detail, more walking
#define OW_TILE_PX           24     // px per tile on screen (480/24 = 20 tiles across)
#define OW_NOISE_SCALE_T     14     // tiles per terrain-noise cell: size of lakes/forests
                                    // (integer so noise is exact on absolute tile indices)
#define OW_WATER_LEVEL       0.30f  // elevation below this = water
#define OW_SAND_LEVEL        0.35f  // ...below this = sand (shoreline)
#define OW_ROCK_LEVEL        0.80f  // elevation above this = rock
#define OW_FOREST_MOIST      0.58f  // moisture above this = forest
#define OW_TRAIL_WIDTH       0.035f // band of the trail noise drawn as dirt trails

// Spawns: one candidate enemy per spawn cell per time window, chosen by hash of
// (cellX, cellY, window). Same cell + window -> same enemy on every device.
#define OW_SPAWN_CELL_T      10     // tiles per spawn cell (60 m)
#define OW_SPAWN_CHANCE      45     // % of cells holding an enemy in a window
#define OW_RARE_CHANCE       12     // % of enemies that are rare (visible only while running)
#define OW_SPAWN_WINDOW_S    600    // s per spawn window (10 min); table re-rolls after
#define OW_DEFEATED_SLOTS    24     // remembered escapes (hidden until their window ends)

// Engaging: walk within range, then STOP to start the chase (design: encounters only
// start when the player is still — safety + resistive touch).
#define OW_ENGAGE_RADIUS_M   14.0f  // m from an enemy that arms the engage prompt
#define OW_STILL_HOLD_S      0.8f   // s of being still (in range) before the chase starts

// Movement classes from ground speed (GPS later; simulated now).
#define LOC_STILL_MS         0.4f   // m/s below this = still
#define LOC_RUN_MS           2.5f   // m/s at/above this = running (else walking)

// Simulated position source (until GPS): drag on the map from where the finger
// lands. Small drag = walk, big drag = run. Sped up so desk testing isn't a hike.
#define LOC_SIM_WALK_MS      1.4f   // m/s real-world walking pace being simulated
#define LOC_SIM_RUN_MS       3.6f   // m/s running pace
#define LOC_SIM_RUN_DRAG     0.75f  // drag magnitude (0..1) at/above which = running
#define LOC_SIM_TIME_SCALE   6.0f   // sim speed-up: 6x = a 60 m spawn cell in ~7 s walking

// Run energy: earned by running, spent as shields in chases (one hit absorbed each).
#define ENERGY_PER_M_RUN     0.5f   // energy per metre covered while running
#define ENERGY_MAX           100.0f
#define ENERGY_PER_SHIELD    50.0f  // energy one shield costs at chase start (max 2)
#define SHIELD_INVULN_S      1.2f   // s of blinking invulnerability after a shield breaks

// Enemy kinds -> chase difficulty.
#define ENEMY_SHADE_GOAL_M   600.0f // common: shorter chase
#define ENEMY_BRUTE_GOAL_M   900.0f // tougher: stage-2 speed
#define ENEMY_PHANTOM_GOAL_M 1200.0f// rare: stage-3 speed, long chase
#define ESCAPE_BONUS_COINS   25     // wallet bonus for escaping (x level)

// ============================================================================
// --- Battles (B1): Pursuit -> (overtaken) -> Hunt ---
// Phase 1 PURSUIT: enemy behind you. Gap grows while you run clean; obstacles make
// you STUMBLE (no death) and cost ground. Gap >= ESCAPE or reaching the goal = escape.
// Gap <= 0 = the enemy overtakes -> Phase 2 HUNT: enemy ahead attacks with its own
// obstacle pattern; orbs strike it; hearts are your life.
#define BATTLE_GAP_START_M     30.0f  // m gap when a battle starts
#define BATTLE_GAP_ESCAPE_M    60.0f  // m gap that counts as escaped
#define BATTLE_GAP_VIS_M       40.0f  // m gap at/above which the pursuer is barely visible
#define BATTLE_GAP_GAIN_L1     0.80f  // m/s gap gained running clean vs a level-1 enemy
#define BATTLE_GAP_GAIN_STEP   0.18f  // m/s less gap gain per enemy level above 1
#define BATTLE_GAP_LOSS_BARRIER 10.0f // m lost stumbling on a barrier
#define BATTLE_GAP_LOSS_WALL   15.0f  // m lost stumbling into a wall
#define BATTLE_STUMBLE_S       1.0f   // s of stumble (slowed, can't be hit again)
#define BATTLE_STUMBLE_SPEED   0.55f  // speed multiplier while stumbling
#define BATTLE_HEARTS          3      // hearts in the Hunt (sneakers may add)
#define BATTLE_OVERTAKE_S      1.4f   // s of the overtake animation (no attacks)
#define BATTLE_HUNT_Z_M        26.0f  // m the enemy runs ahead of you in the Hunt
#define BATTLE_ENEMY_LANE_SPEED 3.0f  // lanes/s the enemy slides between lanes
#define BATTLE_BANNER_S        1.3f   // s a battle banner ("STUMBLE!") stays up
// Per enemy kind: HP, seconds between attacks, % of attacks that also drop an orb.
#define BATTLE_SHADE_HP        5
#define BATTLE_BRUTE_HP        8
#define BATTLE_PHANTOM_HP      10
#define BATTLE_SHADE_ATTACK_S  1.6f
#define BATTLE_BRUTE_ATTACK_S  2.2f
#define BATTLE_PHANTOM_ATTACK_S 1.4f
#define BATTLE_SHADE_ORB_PCT   70
#define BATTLE_BRUTE_ORB_PCT   65
#define BATTLE_PHANTOM_ORB_PCT 55
#define BATTLE_ORB_HIT_DEPTH_M 1.0f   // m collect window for orbs
#define DEFEAT_BONUS_COINS     60     // wallet bonus for defeating an enemy (x level)
