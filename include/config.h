#pragma once
#include <stdint.h>

// ======================
// NeoPixel (your known-good pin)
// ======================
#define LED_PIN 1

#define MATRIX_W 16
#define MATRIX_H 16
#define NUM_LEDS (MATRIX_W * MATRIX_H)

// Global brightness (this is already bright on a 16x16)
#define BRIGHTNESS 95

// ======================
// USB power / dev safety
// ======================
// Goal: full brightness on a USB charger, reduced brightness when a PC/tool opens
// the USB-CDC serial port (helps avoid USB port current limits / glare during development).
// NOTE: intentionally not MCU-USB-stack-specific; it keys off Serial being opened.
#define USB_BRIGHTNESS_AUTO_ENABLED true
#define USB_BRIGHTNESS_WHEN_HOST 45
#define USB_BRIGHTNESS_HOST_LATCH true

// ======================
// Brightness fade
// ======================
// Start at 0 on boot and fade to the selected target.
#define BRIGHTNESS_FADE_UP_MS 900
#define BRIGHTNESS_FADE_DOWN_MS 280

// ======================
// Boot intro: drop-fill + speedup + bang
// ======================
#define INTRO_ENABLED true
// Max time to run the intro (ms). It will stop early if the screen is filled.
#define INTRO_MAX_MS 7500
// Animation speed ramps from START to END as time/fill progresses.
#define INTRO_FRAME_MS_START 20
#define INTRO_FRAME_MS_END 4
// Frames (positions) per falling piece ramps from START to END (lower = faster drop).
#define INTRO_FRAMES_PER_PIECE_START 10
#define INTRO_FRAMES_PER_PIECE_END 3
// End 'bang' effect tuning
#define INTRO_BANG_FLASHES 3
#define INTRO_BANG_FLASH_MS 45
#define INTRO_BANG_RING_MS 18
// ======================
// Boot intro: marquee title
// ======================
#define INTRO_MARQUEE_ENABLED true
#define INTRO_MARQUEE_TEXT "TETRIS"
#define INTRO_MARQUEE_SCALE 2
// Marquee runs up to this many ms (independent of dropfill intro)
#define INTRO_MARQUEE_MAX_MS 4500
// Marquee speed ramps from START to END as it progresses
#define INTRO_MARQUEE_SPEED_MS_START 26
#define INTRO_MARQUEE_SPEED_MS_END 10
// Marquee outro (prevents a visible "jump" at the end on small matrices)
#define INTRO_MARQUEE_END_HOLD_MS 120
#define INTRO_MARQUEE_FADE_FRAMES 8
#define INTRO_MARQUEE_FADE_FRAME_MS 18

// Boot stats (MAX level screen)
#define BOOT_STATS_ENABLED true

// Contrast effect around the MAX-level ("high screen") digits.
//
// HIGH_SCREEN_HALO_ALPHA: 0 = off, 255 = very strong.
// - If HIGH_SCREEN_HALO_DARKEN is false: a *glow* is added by brightening the
//   pixels around the digits (additive, inherits background hue).
// - If HIGH_SCREEN_HALO_DARKEN is true: a *drop shadow* is created by darkening
//   the digit shape at an offset (more visible on diffuser/grid panels).
//
// For a subtle glow try 40–90.
// For a readable drop shadow on LED matrices, 130–190 works well.
#define HIGH_SCREEN_HALO_ALPHA 160
#define HIGH_SCREEN_HALO_DARKEN true

// Drop-shadow tuning (only used when HIGH_SCREEN_HALO_DARKEN is true)
// Offset of the shadow relative to the digit (1,1 = down-right).
#define HIGH_SCREEN_SHADOW_OFFSET_X 1
#define HIGH_SCREEN_SHADOW_OFFSET_Y 1
// Optional soft edge (0 = hard shadow only).
#define HIGH_SCREEN_SHADOW_SOFT_ALPHA 60

// Back-compat alias (older builds used HIGH_SCREEN_GLOW_ALPHA)
#define HIGH_SCREEN_GLOW_ALPHA HIGH_SCREEN_HALO_ALPHA
// ======================
// In-game UI
// ======================
// Show the new level number as a subtle "drop-down" overlay that scrolls down the
// playfield during a level transition.
//
// Default: OFF (cleaner playfield; less visual interruption).
#define LEVEL_NUMBER_DROPDOWN_ENABLED false

// "Decade" milestone (levels 11/21/31/...) border celebration.
//
// Goal: make crossing into a new 10-level border style feel special *without*
// adding any visual clutter inside the playfield.
//
// Default: ON (border-only effect; short duration).
#define MILESTONE_BORDER_REVEAL_ENABLED true
// Duration (ms) of the milestone border reveal animation.
#define MILESTONE_BORDER_REVEAL_MS 1400

// ======================
// Record chase progress background
// ======================
// The MAX level screen (boot + game-over) can show a simple "attempts" meter:
// each completed record-eligible run increments a counter until a new MAX
// level is achieved (then the counter resets to 0).
//
// Visually, this fills the MAX background like a progress bar. Once fully
// filled, the MAX background becomes the new color.
#define MAX_LEVEL_CHASE_PROGRESS_ENABLED true
// Number of "steps" to fill before the MAX background is fully the new color.
// On a 16x16 matrix, 16 steps = 1 row per attempt.
#define MAX_LEVEL_CHASE_PROGRESS_STEPS 16

// Subtle animation on the MAX chase progress fill so it reads as an active
// progress meter (even when attempts are static during the short boot/game-over
// screens).
//
// This is *purely visual*: it does not affect the stored attempt counter.
#define MAX_LEVEL_CHASE_PROGRESS_ANIM_ENABLED true
// How fast the highlight band sweeps UP through the filled region (milliseconds per row).
#define MAX_LEVEL_CHASE_PROGRESS_ANIM_SPEED_MS 90
// Strength of the moving highlight (0..255). Subtle range: ~20–70.
#define MAX_LEVEL_CHASE_PROGRESS_ANIM_ALPHA 45
// Optional trailing softer row behind the highlight (0..255).
#define MAX_LEVEL_CHASE_PROGRESS_ANIM_TAIL_ALPHA 20


// ======================
// USB power / dev safety
// ======================
// Goal: use full brightness on a USB charger, but reduce brightness when a PC/tool opens
// the USB-CDC serial port (helps avoid USB port current limits / glare during development).
// NOTE: This is intentionally not MCU-USB-stack-specific; it keys off Serial being opened.

// ======================
// Brightness fade
// ======================
// Start at 0 on boot and fade to the selected target.


// Wiring layout
#define SERPENTINE true
#define MATRIX_BOTTOM_UP false

// ======================
// Tetris board
// ======================
// NOTE: If your physical frame/bezel clips the outermost LEDs, the bottom row
// can get partially hidden. Setting BOARD_H to 15 reserves y=15 as a "border"
// row so blocks never occupy the clipped pixels, effectively raising the floor
// by 1.
#define BOARD_W 10
#define BOARD_H 15
#define BOARD_OFFSET_X 3
#define BOARD_OFFSET_Y 0


#define RESET_SCORES_ON_BOOT false
#define AI_SAVES_HIGH_SCORE true

// ======================
// AI "smartness" ladder (ties into MAX chase color cycles)
// ======================
// Each time the MAX chase progress hits a *full* background color (i.e., each
// completed fill cycle), the AI can step up its decision quality.
//
// This is separate from the AI speed profile (0..3). The profile affects how
// fast the AI presses buttons; this ladder affects how good its placements are.
#define AI_SMARTNESS_FROM_MAX_CHASE_ENABLED true

// Starting AI skill (1 = current baseline). 0 is intentionally a little "sloppy"
// for testing the ladder.
#define AI_SMARTNESS_BASE 1

// Upper bound on skill (keeps CPU usage predictable on the MCU).
// 1 = baseline heuristic
// 2 = adds more board features (transitions/wells)
// 3 = adds 1-ply next-piece lookahead (when next piece is known)
// 4 = stronger weights + lookahead
#define AI_SMARTNESS_MAX 4
