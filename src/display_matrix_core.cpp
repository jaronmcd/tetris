#include "display_matrix.h"
#include <Arduino.h>
#include <Preferences.h>


MatrixDisplay::MatrixDisplay() : strip_(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800) {}

static inline uint8_t clampBrightnessSetting(uint32_t b) {
  if (b < 8u) b = 8u;
  if (b > 255u) b = 255u;
  return (uint8_t)b;
}

static constexpr const char* DISPLAY_PREFS_NS = "display";
static constexpr const char* DISPLAY_BRIGHTNESS_KEY = "br";
static constexpr const char* DISPLAY_ROTATION_KEY = "rot";
static constexpr const char* DISPLAY_REBOOT_GUARD_KEY = "rbg";

void MatrixDisplay::loadDisplaySettings() {
  Preferences prefs;
  prefs.begin(DISPLAY_PREFS_NS, false);
  uint32_t savedBrightness = prefs.getUInt(DISPLAY_BRIGHTNESS_KEY, (uint32_t)BRIGHTNESS);
  const uint32_t savedRotation = prefs.getUInt(DISPLAY_ROTATION_KEY, 0u);

#if SAFE_REBOOT_BRIGHTNESS_GUARD_ENABLED
  rebootBrightnessWasClamped_ = false;
  const bool previousBootUnclean = prefs.getBool(DISPLAY_REBOOT_GUARD_KEY, false);
  const uint8_t safeBrightness = clampBrightnessSetting((uint32_t)SAFE_REBOOT_BRIGHTNESS);
  if (previousBootUnclean && savedBrightness > (uint32_t)safeBrightness) {
    savedBrightness = (uint32_t)safeBrightness;
    prefs.putUInt(DISPLAY_BRIGHTNESS_KEY, savedBrightness);
    rebootBrightnessWasClamped_ = true;
  }

  // Mark this boot as "in progress". We clear it after stable uptime.
  prefs.putBool(DISPLAY_REBOOT_GUARD_KEY, true);
  rebootGuardArmed_ = true;
  rebootGuardArmMs_ = millis();
#else
  rebootBrightnessWasClamped_ = false;
  rebootGuardArmed_ = false;
  rebootGuardArmMs_ = 0;
#endif

  prefs.end();

  userBrightness_ = clampBrightnessSetting(savedBrightness);
  rotationQuarterTurns_ = (uint8_t)(savedRotation & 0x03u);

#if SAFE_REBOOT_BRIGHTNESS_GUARD_ENABLED
  if (rebootBrightnessWasClamped_) {
    Serial.printf("Display: unclean reboot detected, brightness clamped to safe level (%u).\n",
                  (unsigned)userBrightness_);
  }
#endif
}

void MatrixDisplay::saveDisplaySettings() const {
  Preferences prefs;
  prefs.begin(DISPLAY_PREFS_NS, false);
  prefs.putUInt(DISPLAY_BRIGHTNESS_KEY, (uint32_t)userBrightness_);
  prefs.putUInt(DISPLAY_ROTATION_KEY, (uint32_t)rotationQuarterTurns_);
  prefs.end();
}

void MatrixDisplay::disarmRebootBrightnessGuard() {
#if SAFE_REBOOT_BRIGHTNESS_GUARD_ENABLED
  if (!rebootGuardArmed_) return;

  Preferences prefs;
  prefs.begin(DISPLAY_PREFS_NS, false);
  prefs.putBool(DISPLAY_REBOOT_GUARD_KEY, false);
  prefs.end();

  rebootGuardArmed_ = false;
#endif
}

void MatrixDisplay::adjustUserBrightness(int8_t delta) {
  int16_t next = (int16_t)userBrightness_ + (int16_t)delta;
  if (next < 8) next = 8;
  if (next > 255) next = 255;
  const uint8_t nv = (uint8_t)next;
  if (nv == userBrightness_) return;
  userBrightness_ = nv;
  saveDisplaySettings();
}

void MatrixDisplay::rotateDisplay(int8_t quarterTurnsDelta) {
  int8_t next = (int8_t)rotationQuarterTurns_ + quarterTurnsDelta;
  next %= 4;
  if (next < 0) next += 4;
  const uint8_t nv = (uint8_t)next;
  if (nv == rotationQuarterTurns_) return;
  rotationQuarterTurns_ = nv;
  saveDisplaySettings();
}

void MatrixDisplay::tickPowerBrightness(uint32_t nowMs) {
#if SAFE_REBOOT_BRIGHTNESS_GUARD_ENABLED
  if (rebootGuardArmed_) {
    const uint32_t aliveMs = (uint32_t)(nowMs - rebootGuardArmMs_);
    if (aliveMs >= (uint32_t)SAFE_REBOOT_STABLE_UPTIME_MS) {
      disarmRebootBrightnessGuard();
    }
  }
#endif

  uint8_t target = userBrightness_;
#if PAUSE_DIM_ENABLED
  if (paused_ && target > (uint8_t)PAUSE_BRIGHTNESS_WHEN_PAUSED) {
    target = (uint8_t)PAUSE_BRIGHTNESS_WHEN_PAUSED;
  }
#endif

  auto lerpU8 = [](uint8_t a, uint8_t b, uint32_t num, uint32_t den) -> uint8_t {
    if (den == 0) return b;
    const int32_t delta = (int32_t)b - (int32_t)a;
    const int32_t v = (int32_t)a + (delta * (int32_t)num) / (int32_t)den;
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (uint8_t)v;
  };

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
  loadDisplaySettings();

  // Start dark to avoid a bright flash, then fade up to the selected target.
  strip_.begin();
  strip_.setBrightness(0);
  strip_.clear();
  strip_.show();

  brightnessApplied_ = 0;
  brightnessTarget_ = 0;
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
  uint8_t rx = x;
  uint8_t ry = y;
  switch (rotationQuarterTurns_ & 0x03u) {
    case 1: // 90 CW
      rx = (uint8_t)(MATRIX_W - 1 - y);
      ry = x;
      break;
    case 2: // 180
      rx = (uint8_t)(MATRIX_W - 1 - x);
      ry = (uint8_t)(MATRIX_H - 1 - y);
      break;
    case 3: // 270 CW
      rx = y;
      ry = (uint8_t)(MATRIX_H - 1 - x);
      break;
    default:
      break;
  }

  uint8_t xx = (uint8_t)(MATRIX_W - 1 - rx);
  uint8_t yy = MATRIX_BOTTOM_UP ? (MATRIX_H - 1 - ry) : ry;
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
