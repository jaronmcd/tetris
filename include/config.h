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
// Development safety (USB-connected)
// ======================
// When connected to a PC over native USB (USB-CDC), optionally start at a
// lower brightness to reduce current draw / glare during development.
//
// Detection: if a USB-CDC host opens the serial port during the first
// USB_DEV_DETECT_WINDOW_MS after boot, we treat that as "dev on PC" and
// start at USB_DEV_SAFE_BRIGHTNESS.
#define USB_DEV_SAFE_BRIGHTNESS_ENABLED true
#define USB_DEV_SAFE_BRIGHTNESS 6
#define USB_DEV_DETECT_WINDOW_MS 1500

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