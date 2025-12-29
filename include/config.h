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
#define BRIGHTNESS 14

// Wiring layout
#define SERPENTINE true
#define MATRIX_BOTTOM_UP false

// ======================
// Tetris board (10x16 centered on 16x16)
// ======================
#define BOARD_W 10
#define BOARD_H 16
#define BOARD_OFFSET_X 3
#define BOARD_OFFSET_Y 0


#define RESET_SCORES_ON_BOOT false
#define AI_SAVES_HIGH_SCORE true