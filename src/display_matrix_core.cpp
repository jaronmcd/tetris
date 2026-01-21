#include "display_matrix.h"
#include <Arduino.h>


MatrixDisplay::MatrixDisplay() : strip_(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800) {}



static inline uint8_t chooseBrightnessTarget(bool& hostLatched) {
#if USB_BRIGHTNESS_AUTO_ENABLED
  const bool hostSignal = (bool)Serial; // true when USB-CDC port is opened by host
  if (USB_BRIGHTNESS_HOST_LATCH && hostSignal) {
    hostLatched = true;
  }
  if (hostLatched || hostSignal) return (uint8_t)USB_BRIGHTNESS_WHEN_HOST;
#endif
  return (uint8_t)BRIGHTNESS;
}

static inline uint8_t lerpU8(uint8_t a, uint8_t b, uint32_t num, uint32_t den) {
  if (den == 0) return b;
  const int32_t delta = (int32_t)b - (int32_t)a;
  const int32_t v = (int32_t)a + (delta * (int32_t)num) / (int32_t)den;
  if (v < 0) return 0;
  if (v > 255) return 255;
  return (uint8_t)v;
}

void MatrixDisplay::tickPowerBrightness(uint32_t nowMs) {
  const uint8_t target = chooseBrightnessTarget(hostLatched_);

  // Start a new fade if target changes.
  if (target != brightnessTarget_) {
    brightnessTarget_ = target;
    fadeFrom_ = brightnessApplied_;
    fadeTo_ = target;
    fadeStartMs_ = nowMs;

    // Fade duration depends on direction.
    fadeDurationMs_ = (fadeTo_ >= fadeFrom_) ? (uint32_t)BRIGHTNESS_FADE_UP_MS
                                             : (uint32_t)BRIGHTNESS_FADE_DOWN_MS;

    fadeActive_ = true;

    if (fadeDurationMs_ == 0) {
      strip_.setBrightness(fadeTo_);
      brightnessApplied_ = fadeTo_;
      fadeActive_ = false;
    }
    return;
  }

  // Advance active fade.
  if (!fadeActive_) return;

  const uint32_t elapsed = (uint32_t)(nowMs - fadeStartMs_);
  if (elapsed >= fadeDurationMs_) {
    strip_.setBrightness(fadeTo_);
    brightnessApplied_ = fadeTo_;
    fadeActive_ = false;
    return;
  }

  const uint8_t b = lerpU8(fadeFrom_, fadeTo_, elapsed, fadeDurationMs_);
  if (b != brightnessApplied_) {
    strip_.setBrightness(b);
    brightnessApplied_ = b;
  }
}

void MatrixDisplay::begin() {
  // Start dark to avoid a bright flash, then fade up to the selected target.
  strip_.begin();
  strip_.setBrightness(0);
  strip_.clear();
  strip_.show();

  brightnessApplied_ = 0;
  brightnessTarget_ = 0;
  hostLatched_ = false;
  fadeActive_ = false;

  // Kick the first fade.
  tickPowerBrightness(millis());
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

