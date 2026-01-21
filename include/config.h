#pragma once
#include <stdint.h>

#define LED_PIN 1

#define MATRIX_W 16
#define MATRIX_H 16
#define NUM_LEDS (MATRIX_W * MATRIX_H)

// "Full" brightness target (e.g., on a USB charger / wall brick).
#define BRIGHTNESS 95

// Auto-brightness using a *generic* signal: whether the USB-CDC serial port has been opened.
// Rationale: a USB charger can't open a serial port; a PC/tooling (PlatformIO monitor, etc.) often does.
// This avoids ESP32-S3/TinyUSB-specific enumeration APIs.
//
// Behavior:
// - Default = BRIGHTNESS (charger/high power)
// - If the USB-CDC serial port is opened at any time: switch to USB_BRIGHTNESS_WHEN_HOST (dim) and latch.
#define USB_BRIGHTNESS_AUTO_ENABLED true
#define USB_BRIGHTNESS_WHEN_HOST 15

// Kept for compatibility with existing code paths.
// When true, we treat "Serial opened" as the host signal.
#define USB_BRIGHTNESS_USE_SERIAL_OPEN_FALLBACK true

// Optional: once dimmed due to Serial-open, keep it dim until reboot.
#define USB_BRIGHTNESS_HOST_LATCH true

#define SERPENTINE true
#define MATRIX_BOTTOM_UP false

#define BOARD_W 10
#define BOARD_H 15
#define BOARD_OFFSET_X 3
#define BOARD_OFFSET_Y 0

#define RESET_SCORES_ON_BOOT false
#define AI_SAVES_HIGH_SCORE true
