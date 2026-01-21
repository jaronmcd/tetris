#pragma once
#include <stdint.h>

#define LED_PIN 1

#define MATRIX_W 16
#define MATRIX_H 16
#define NUM_LEDS (MATRIX_W * MATRIX_H)

#define BRIGHTNESS 95

#define USB_BRIGHTNESS_AUTO_ENABLED true
#define USB_BRIGHTNESS_WHEN_HOST 15
#define USB_BRIGHTNESS_CHARGER_DELAY_MS 5000
// Fallback: treat an opened USB-CDC serial port as "host" even if tud_mounted()
// is unavailable or unreliable on some stacks.
#define USB_BRIGHTNESS_USE_SERIAL_OPEN_FALLBACK true

// Safety: if we initially assume "charger" and later detect a real USB host
// (e.g., enumeration completes, or the USB-CDC port is opened), force host
// brightness.
#define USB_BRIGHTNESS_HOST_OVERRIDE true


#define SERPENTINE true
#define MATRIX_BOTTOM_UP false

#define BOARD_W 10
#define BOARD_H 15
#define BOARD_OFFSET_X 3
#define BOARD_OFFSET_Y 0

#define RESET_SCORES_ON_BOOT false
#define AI_SAVES_HIGH_SCORE true
