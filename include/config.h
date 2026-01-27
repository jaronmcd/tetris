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
#define USB_BRIGHTNESS_WHEN_HOST 15
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

// Boot stats (MAX level screen)
#define BOOT_STATS_ENABLED false

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
