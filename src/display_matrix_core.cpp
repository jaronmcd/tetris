#include "display_matrix.h"
#include <Arduino.h>


MatrixDisplay::MatrixDisplay() : strip_(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800) {}



static uint8_t chooseStartupBrightness() {
#if USB_DEV_SAFE_BRIGHTNESS_ENABLED && (defined(ARDUINO_USB_CDC_ON_BOOT) || defined(ARDUINO_USB_MODE))
  const uint32_t t0 = millis();
  while ((millis() - t0) < (uint32_t)USB_DEV_DETECT_WINDOW_MS) {
    // On native USB-CDC, bool(Serial) becomes true when the host opens the port.
    if (Serial) return (uint8_t)USB_DEV_SAFE_BRIGHTNESS;
    delay(25);
  }
#endif
  return (uint8_t)BRIGHTNESS;
}

void MatrixDisplay::begin() {
  strip_.begin();
  const uint8_t b = chooseStartupBrightness();
  strip_.setBrightness(b);
  #if USB_DEV_SAFE_BRIGHTNESS_ENABLED && (defined(ARDUINO_USB_CDC_ON_BOOT) || defined(ARDUINO_USB_MODE))
  if (b == (uint8_t)USB_DEV_SAFE_BRIGHTNESS && Serial) {
    Serial.print("USB dev detected; brightness set to ");
    Serial.println(b);
  }
  #endif
  strip_.clear();
  strip_.show();
}


void MatrixDisplay::bootFlash() {
  strip_.fill(strip_.Color(20, 0, 0)); strip_.show(); delay(120);
  strip_.fill(strip_.Color(0, 20, 0)); strip_.show(); delay(120);
  strip_.fill(strip_.Color(0, 0, 20)); strip_.show(); delay(120);
  strip_.clear(); strip_.show();
}


uint16_t MatrixDisplay::XY(uint8_t x, uint8_t y) const {
  if (x >= MATRIX_W || y >= MATRIX_H) return 0;
  uint8_t xx = (uint8_t)(MATRIX_W - 1 - x);
  uint8_t yy = MATRIX_BOTTOM_UP ? (MATRIX_H - 1 - y) : y;
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

