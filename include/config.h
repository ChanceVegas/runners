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

// --- Colors (RGB565) ---
#define COLOR_BG_DEBUG       0x0000 // black
#define COLOR_TOUCH_DEBUG    0x07E0 // green — R0 touch-point marker
