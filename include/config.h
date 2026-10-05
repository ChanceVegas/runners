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

// --- Input: single-touch drag gestures (carried over, hw-verified 2026-07-13) ---
// R1 re-maps these to lane swipes; constants stay until R1 measures otherwise.
#define INPUT_DEADZONE_PX    10     // px from anchor before movement registers; kills resistive jitter
#define INPUT_JOY_RANGE_PX   60     // px of drag = full deflection
#define INPUT_JUMP_FLICK_PX  30     // finger must rise this far above anchor to jump
#define INPUT_JUMP_REARM_PX  15     // finger must return within this of anchor to re-arm jump
#define INPUT_EMA_ALPHA      0.5f   // touch smoothing 0..1; higher = snappier, noisier
#define INPUT_JUMP_VEL_PX    14     // upward px between samples = flick (touch ~25 Hz)
#define INPUT_SWIPE_PX       40     // px of horizontal drag = one lane change. The anchor
                                    // re-centres after each swipe, so one long drag can
                                    // cross two lanes. Lower = twitchier.

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
#define RUN_LANE_SPEED       7.0f   // lanes/s sideways; 1/7 s per lane change
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
