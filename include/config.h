#pragma once

// ======================
// NeoPixel matrix config
// ======================
#define MATRIX_W 16
#define MATRIX_H 16

// Change this to the GPIO you wired to DIN on the NeoPixel matrix:
#define LED_PIN 1

// NeoPixel color order (most WS2812B matrices are GRB)
#define NEO_PIXEL_TYPE (NEO_GRB + NEO_KHZ800)

// Brightness 0-255 (keep modest unless you have a beefy 5V supply)
#define BRIGHTNESS 40

// ======================
// Matrix wiring layout
// ======================
// SERPENTINE = true for "zig-zag" rows (very common on 16x16 panels)
#define SERPENTINE true

// If your panel is wired bottom-up, set true (common depending on panel orientation)
#define MATRIX_BOTTOM_UP false

// ======================
// Tetris playfield config
// ======================
// 10x16 fits perfectly in 16x16 with side margins
#define BOARD_W 10
#define BOARD_H 16

// Center 10-wide board on 16-wide matrix => (16-10)/2 = 3
#define BOARD_OFFSET_X 3
#define BOARD_OFFSET_Y 0

// ======================
// Optional buttons (active LOW to GND, use INPUT_PULLUP)
// Set to -1 to disable that button.
// ======================
#define BTN_LEFT_PIN   -1
#define BTN_RIGHT_PIN  -1
#define BTN_ROTATE_PIN -1
#define BTN_DOWN_PIN   -1
#define BTN_DROP_PIN   -1

// ======================
// Serial controls (when using Serial Monitor @ 115200)
// ======================
// a = left, d = right, w = rotate, s = soft drop, space = hard drop, r = restart
