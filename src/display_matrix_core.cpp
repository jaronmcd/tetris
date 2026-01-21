#include "display_matrix.h"

#include <Arduino.h>


#if USB_BRIGHTNESS_AUTO_ENABLED && USB_BRIGHTNESS_USE_SERIAL_OPEN_FALLBACK
static inline bool usbHostSignal() {
  #if defined(ARDUINO_USB_CDC_ON_BOOT) && (ARDUINO_USB_CDC_ON_BOOT)
    // For USB-CDC builds, `Serial` becomes truthy when a host opens the CDC port (DTR).
    // This is a simple, non-hardware-specific "PC present" hint; a USB charger can't do this.
    return (bool)Serial;
  #else
    return false;
  #endif
}
#else
static inline bool usbHostSignal() { return false; }
#endif


MatrixDisplay::MatrixDisplay()
  : strip_(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800) {}

void MatrixDisplay::applyBrightness(uint8_t b) {
  if (brightnessApplied_ == b) return;
  brightnessApplied_ = b;
  strip_.setBrightness(b);
}

void MatrixDisplay::begin() {
#if USB_BRIGHTNESS_AUTO_ENABLED
  usbBrightnessStartMs_ = millis();
  usbMode_ = UsbPowerMode::Unknown;

  // Default to full brightness (e.g., USB charger / wall brick).
  // If a PC/tool opens the USB-CDC serial port, tickUsbPowerBrightness() will
  // drop to USB_BRIGHTNESS_WHEN_HOST.
  applyBrightness((uint8_t)BRIGHTNESS);
#else
  applyBrightness((uint8_t)BRIGHTNESS);
#endif

  strip_.begin();
  strip_.clear();
  strip_.show();
}


void MatrixDisplay::tickUsbPowerBrightness(uint32_t nowMs) {
#if !USB_BRIGHTNESS_AUTO_ENABLED
  (void)nowMs;
  return;
#else
  (void)nowMs;

  const bool serialOpened = usbHostSignal();

  // If a PC/tool opens the USB-CDC serial port, go dim.
  if (serialOpened) {
  #if USB_BRIGHTNESS_HOST_LATCH
    usbMode_ = UsbPowerMode::Host;
  #else
    usbMode_ = UsbPowerMode::Unknown;
  #endif
    applyBrightness((uint8_t)USB_BRIGHTNESS_WHEN_HOST);
    return;
  }

  // If we latched Host previously, stay dim until reboot.
#if USB_BRIGHTNESS_HOST_LATCH
  if (usbMode_ == UsbPowerMode::Host) {
    applyBrightness((uint8_t)USB_BRIGHTNESS_WHEN_HOST);
    return;
  }
#endif

  // Default: full brightness.
  usbMode_ = UsbPowerMode::Unknown;
  applyBrightness((uint8_t)BRIGHTNESS);
#endif
}


void MatrixDisplay::bootFlash() {
  strip_.clear();
  strip_.fill(strip_.Color(255, 0, 0));
  strip_.show();
  delay(120);

  strip_.clear();
  strip_.fill(strip_.Color(0, 255, 0));
  strip_.show();
  delay(120);

  strip_.clear();
  strip_.fill(strip_.Color(0, 0, 255));
  strip_.show();
  delay(120);

  strip_.clear();
  strip_.show();
}

uint16_t MatrixDisplay::XY(uint8_t x, uint8_t y) const {
  if (x >= MATRIX_W || y >= MATRIX_H) return 0;
  // Match original physical mapping:
  // - X is mirrored (MATRIX_W-1-x)
  // - Optional Y flip via MATRIX_BOTTOM_UP
  // - Optional serpentine wiring via SERPENTINE
  uint8_t xx = (uint8_t)(MATRIX_W - 1 - x);
  uint8_t yy = MATRIX_BOTTOM_UP ? (uint8_t)(MATRIX_H - 1 - y) : y;
  if (!SERPENTINE) return (uint16_t)yy * MATRIX_W + xx;
  if ((yy & 1) == 0) return (uint16_t)yy * MATRIX_W + xx;
  return (uint16_t)yy * MATRIX_W + (MATRIX_W - 1 - xx);
}

void MatrixDisplay::setPixel(int16_t x, int16_t y, uint32_t c) {
  if (x < 0 || x >= MATRIX_W || y < 0 || y >= MATRIX_H) return;
  strip_.setPixelColor(XY((uint8_t)x, (uint8_t)y), c);
}

uint32_t MatrixDisplay::rgb(uint32_t rrggbb) const {
  uint8_t r = (rrggbb >> 16) & 0xFF;
  uint8_t g = (rrggbb >> 8) & 0xFF;
  uint8_t b = (rrggbb) & 0xFF;
  return strip_.Color(r, g, b);
}
