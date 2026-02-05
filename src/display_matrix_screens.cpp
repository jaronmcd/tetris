#include "display_matrix.h"
#include <Arduino.h>

// ESP32 has a hardware RNG we can use for truly random boot intros.
#if defined(ARDUINO_ARCH_ESP32)
#include "esp_system.h" // esp_random()
#endif

// Local 5x7 thin-line digit font for numeric screens (columns, LSB=top row).
static const uint8_t DIGITS_5x7[10][5] = {
  {0x3E, 0x41, 0x41, 0x41, 0x3E}, // 0
  {0x00, 0x42, 0x7F, 0x40, 0x00}, // 1
  {0x42, 0x61, 0x51, 0x49, 0x46}, // 2
  {0x22, 0x41, 0x49, 0x49, 0x36}, // 3
  {0x18, 0x14, 0x12, 0x7F, 0x10}, // 4
  {0x27, 0x45, 0x45, 0x45, 0x39}, // 5
  {0x3C, 0x4A, 0x49, 0x49, 0x30}, // 6
  {0x01, 0x71, 0x09, 0x05, 0x03}, // 7
  {0x36, 0x49, 0x49, 0x49, 0x36}, // 8
  {0x06, 0x49, 0x49, 0x29, 0x1E}, // 9
};


uint32_t MatrixDisplay::dimColor(uint32_t c, uint8_t alpha) const {
  uint8_t r = (c >> 16) & 0xFF;
  uint8_t g = (c >> 8) & 0xFF;
  uint8_t b = (c >> 0) & 0xFF;
  r = (uint8_t)((uint16_t)r * alpha / 255);
  g = (uint8_t)((uint16_t)g * alpha / 255);
  b = (uint8_t)((uint16_t)b * alpha / 255);
  return strip_.Color(r, g, b);
}

void MatrixDisplay::drawDigit5x7Scaled(uint8_t digit, int16_t x, int16_t y, uint8_t scale, uint32_t color) {
  if (digit > 9) return;
  const uint8_t* cols = DIGITS_5x7[digit];

  for (int col = 0; col < 5; col++) {
    uint8_t bits = cols[col];
    for (int row = 0; row < 7; row++) {
      if ((bits >> row) & 1) {
        for (uint8_t sy = 0; sy < scale; sy++) {
          for (uint8_t sx = 0; sx < scale; sx++) {
            setPixel(x + (col * scale) + sx, y + (row * scale) + sy, color);
          }
        }
      }
    }
  }
}

// Adds a subtle contrast effect around a digit.
// - darken=false: additive glow so the effect inherits the background hue.
// - darken=true: directional drop shadow by darkening the digit mask at an offset.
// - Uses a per-call mask so we don't "stack" modifications repeatedly in the same frame.
void MatrixDisplay::drawDigit5x7ScaledHalo(uint8_t digit, int16_t x, int16_t y, uint8_t scale,
                                          uint32_t color, uint8_t haloAlpha, bool darken, uint8_t* haloMask) {
  if (digit > 9) return;
  if (!haloMask || haloAlpha == 0) {
    // No halo, just draw the digit.
    drawDigit5x7Scaled(digit, x, y, scale, color);
    return;
  }

  const uint8_t* cols = DIGITS_5x7[digit];

  const uint32_t haloAdd = dimColor(color, haloAlpha);

  auto darkenAt = [&](int16_t gx, int16_t gy, uint8_t alpha) {
    if (gx < 0 || gx >= (int16_t)MATRIX_W || gy < 0 || gy >= (int16_t)MATRIX_H) return;
    const uint16_t mi = (uint16_t)((uint16_t)gy * (uint16_t)MATRIX_W + (uint16_t)gx);
    if (haloMask[mi]) return;
    haloMask[mi] = 1;

    const uint16_t idx = XY((uint8_t)gx, (uint8_t)gy);
    uint32_t old = strip_.getPixelColor(idx);
    uint8_t or_ = (uint8_t)((old >> 16) & 0xFF);
    uint8_t og_ = (uint8_t)((old >> 8) & 0xFF);
    uint8_t ob_ = (uint8_t)(old & 0xFF);

    // Blend toward black by scaling the current pixel down.
    const uint16_t factor = (uint16_t)(255 - alpha); // 0..255
    uint8_t nr = (uint8_t)((uint16_t)or_ * factor / 255);
    uint8_t ng = (uint8_t)((uint16_t)og_ * factor / 255);
    uint8_t nb = (uint8_t)((uint16_t)ob_ * factor / 255);
    strip_.setPixelColor(idx, strip_.Color(nr, ng, nb));
  };

  auto glowAt = [&](int16_t gx, int16_t gy) {
    if (gx < 0 || gx >= (int16_t)MATRIX_W || gy < 0 || gy >= (int16_t)MATRIX_H) return;
    const uint16_t mi = (uint16_t)((uint16_t)gy * (uint16_t)MATRIX_W + (uint16_t)gx);
    if (haloMask[mi]) return;
    haloMask[mi] = 1;

    const uint16_t idx = XY((uint8_t)gx, (uint8_t)gy);
    uint32_t old = strip_.getPixelColor(idx);
    uint8_t or_ = (uint8_t)((old >> 16) & 0xFF);
    uint8_t og_ = (uint8_t)((old >> 8) & 0xFF);
    uint8_t ob_ = (uint8_t)(old & 0xFF);

    // Additive glow: brighten the current pixel (clamped).
    uint16_t r = (uint16_t)or_ + (uint16_t)((haloAdd >> 16) & 0xFF);
    uint16_t g = (uint16_t)og_ + (uint16_t)((haloAdd >> 8) & 0xFF);
    uint16_t b = (uint16_t)ob_ + (uint16_t)(haloAdd & 0xFF);

    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;

    strip_.setPixelColor(idx, strip_.Color((uint8_t)r, (uint8_t)g, (uint8_t)b));
  };

  if (darken) {
    // Directional drop shadow: darken a shifted copy of the digit mask, then
    // draw the digit on top. This tends to read much better through diffuser
    // grids than a 1px outline.
    const int8_t offX = (int8_t)HIGH_SCREEN_SHADOW_OFFSET_X;
    const int8_t offY = (int8_t)HIGH_SCREEN_SHADOW_OFFSET_Y;
    const uint8_t softA = (uint8_t)HIGH_SCREEN_SHADOW_SOFT_ALPHA;

    // Core shadow pass.
    for (int col = 0; col < 5; col++) {
      uint8_t bits = cols[col];
      for (int row = 0; row < 7; row++) {
        if (!((bits >> row) & 1)) continue;

        for (uint8_t sy = 0; sy < scale; sy++) {
          for (uint8_t sx = 0; sx < scale; sx++) {
            const int16_t px = (int16_t)(x + (int16_t)(col * scale) + (int16_t)sx);
            const int16_t py = (int16_t)(y + (int16_t)(row * scale) + (int16_t)sy);
            darkenAt((int16_t)(px + offX), (int16_t)(py + offY), haloAlpha);
          }
        }
      }
    }

    // Optional soft edge: extend the shadow slightly further in the same
    // direction to simulate a tiny blur.
    if (softA > 0) {
      for (int col = 0; col < 5; col++) {
        uint8_t bits = cols[col];
        for (int row = 0; row < 7; row++) {
          if (!((bits >> row) & 1)) continue;

          for (uint8_t sy = 0; sy < scale; sy++) {
            for (uint8_t sx = 0; sx < scale; sx++) {
              const int16_t px = (int16_t)(x + (int16_t)(col * scale) + (int16_t)sx);
              const int16_t py = (int16_t)(y + (int16_t)(row * scale) + (int16_t)sy);

              // One extra pixel of spread in the shadow direction.
              darkenAt((int16_t)(px + offX + (offX >= 0 ? 1 : -1)), (int16_t)(py + offY), softA);
              darkenAt((int16_t)(px + offX), (int16_t)(py + offY + (offY >= 0 ? 1 : -1)), softA);
              darkenAt((int16_t)(px + offX + (offX >= 0 ? 1 : -1)), (int16_t)(py + offY + (offY >= 0 ? 1 : -1)), softA);
            }
          }
        }
      }
    }

    drawDigit5x7Scaled(digit, x, y, scale, color);
    return;
  }

  // Glow pass: for every "on" pixel of the scaled digit, brighten its 8-neighborhood.
  for (int col = 0; col < 5; col++) {
    uint8_t bits = cols[col];
    for (int row = 0; row < 7; row++) {
      if (!((bits >> row) & 1)) continue;

      for (uint8_t sy = 0; sy < scale; sy++) {
        for (uint8_t sx = 0; sx < scale; sx++) {
          const int16_t px = (int16_t)(x + (int16_t)(col * scale) + (int16_t)sx);
          const int16_t py = (int16_t)(y + (int16_t)(row * scale) + (int16_t)sy);

          for (int8_t dy = -1; dy <= 1; dy++) {
            for (int8_t dx = -1; dx <= 1; dx++) {
              if (dx == 0 && dy == 0) continue;
              glowAt((int16_t)(px + dx), (int16_t)(py + dy));
            }
          }
        }
      }
    }
  }

  // Digit pass (crisp on top of the halo)
  drawDigit5x7Scaled(digit, x, y, scale, color);
}

void MatrixDisplay::drawNumberCentered(uint8_t value, uint8_t scale, uint32_t color) {
  if (value > 99) value = 99;

  uint8_t d0 = (uint8_t)(value / 10);
  uint8_t d1 = (uint8_t)(value % 10);
  const bool twoDigits = (value >= 10);

  // Auto-fit the requested scale to the physical matrix.
  // On a 16x16 display, 5x7 digits at scale=2 fit for 1-digit values,
  // but 2-digit values would clip. This keeps the "NEW MAX" / party
  // overlay readable for 10+.
  uint8_t s = (scale < 1) ? 1 : scale;
  while (s > 1) {
    const int digitW = 5 * s;
    const int digitH = 7 * s;
    const int spacing = s; // 1*scale spacing
    const int totalW = twoDigits ? (digitW * 2 + spacing) : digitW;
    if (totalW <= MATRIX_W && digitH <= MATRIX_H) break;
    s--;
  }

  const int digitW = 5 * s;
  const int digitH = 7 * s;
  const int spacing = s; // 1*scale spacing
  const int totalW = twoDigits ? (digitW * 2 + spacing) : digitW;

  int16_t startX = (int16_t)((MATRIX_W - totalW) / 2);
  int16_t startY = (int16_t)((MATRIX_H - digitH) / 2);

  if (startX < 0) startX = 0;
  if (startY < 0) startY = 0;

  if (!twoDigits) {
    drawDigit5x7Scaled(d1, startX, startY, s, color);
  } else {
    drawDigit5x7Scaled(d0, startX, startY, s, color);
    drawDigit5x7Scaled(d1, (int16_t)(startX + digitW + spacing), startY, s, color);
  }

}

void MatrixDisplay::drawNumberCenteredHalo(uint8_t value, uint8_t scale, uint32_t color, uint8_t haloAlpha, bool darken) {
  if (haloAlpha == 0) {
    drawNumberCentered(value, scale, color);
    return;
  }

  if (value > 99) value = 99;

  uint8_t d0 = (uint8_t)(value / 10);
  uint8_t d1 = (uint8_t)(value % 10);
  const bool twoDigits = (value >= 10);

  // Match drawNumberCentered's auto-fit behavior.
  uint8_t s = (scale < 1) ? 1 : scale;
  while (s > 1) {
    const int digitW = 5 * s;
    const int digitH = 7 * s;
    const int spacing = s;
    const int totalW = twoDigits ? (digitW * 2 + spacing) : digitW;
    if (totalW <= MATRIX_W && digitH <= MATRIX_H) break;
    s--;
  }

  const int digitW = 5 * s;
  const int digitH = 7 * s;
  const int spacing = s;
  const int totalW = twoDigits ? (digitW * 2 + spacing) : digitW;

  int16_t startX = (int16_t)((MATRIX_W - totalW) / 2);
  int16_t startY = (int16_t)((MATRIX_H - digitH) / 2);

  if (startX < 0) startX = 0;
  if (startY < 0) startY = 0;

  // Per-frame mask so a halo pixel is only modified once.
  uint8_t mask[MATRIX_W * MATRIX_H] = {};

  if (!twoDigits) {
    drawDigit5x7ScaledHalo(d1, startX, startY, s, color, haloAlpha, darken, mask);
  } else {
    drawDigit5x7ScaledHalo(d0, startX, startY, s, color, haloAlpha, darken, mask);
    drawDigit5x7ScaledHalo(d1, (int16_t)(startX + digitW + spacing), startY, s, color, haloAlpha, darken, mask);
  }
}

bool MatrixDisplay::showLevelNumberScreen(uint8_t value, uint32_t bg, uint32_t fg, uint32_t durationMs, AbortFn abortFn) {
  const uint8_t scale = 1; // 1px-stroke digits (avoid chunky "block" look)
  const uint32_t start = millis();

  while ((millis() - start) < durationMs) {
    if (abortFn && abortFn()) return true;

    tickPowerBrightness(millis());

    strip_.fill(bg);
    // Match the MAX-level number styling: optional halo that can be configured
    // as an additive glow or (preferably for diffuser grids) a directional
    // drop shadow.
    drawNumberCenteredHalo(value, scale, fg,
                           (uint8_t)HIGH_SCREEN_HALO_ALPHA,
                           (bool)HIGH_SCREEN_HALO_DARKEN);
    strip_.show();
    delay(25);
  }
  return false;
}

void MatrixDisplay::fillMaxChaseBackground(uint32_t baseBg, uint32_t chaseBg, uint16_t maxChaseAttempts, uint32_t nowMs) {
#if MAX_LEVEL_CHASE_PROGRESS_ENABLED
  uint16_t steps = (uint16_t)MAX_LEVEL_CHASE_PROGRESS_STEPS;
  if (steps < 1) steps = 1;

  // "Attempts to beat MAX" background that never caps:
  // - The meter fills over `steps` attempts.
  // - When the meter hits full (attempts % steps == 0), the *entire* background becomes the new color.
  // - Further attempts start a new fill cycle, using a new (cycled) color.
  //
  // This makes the record screen act like a persistent "chase history" indicator
  // rather than saturating to one color forever.
  const uint32_t cycle = (uint32_t)maxChaseAttempts / (uint32_t)steps;   // how many full fills completed
  const uint16_t inCycle = (uint16_t)(maxChaseAttempts % steps);          // progress within current fill

  // Color selection:
  // - cycle 0 background uses the provided baseBg (purple).
  // - cycle 1 background uses the provided chaseBg (teal) to preserve the original look.
  // - cycle 2+ backgrounds use a HSV hue sweep (golden-angle step) to avoid repetition.
  auto cycleColor = [&](uint32_t idx) -> uint32_t {
    if (idx == 0) return baseBg;
    if (idx == 1) return chaseBg;

    // Golden-angle hue step gives a well-distributed "infinite" palette.
    // (16-bit hue wraps naturally.)
    const uint16_t hue = (uint16_t)(idx * 0x9E37u);

    // Match the existing MAX screen dimness (the base/chase colors are already dimmed by callers).
    return dimColor(strip_.ColorHSV(hue, 255, 255), 95);
  };

  const uint32_t bgBase = cycleColor(cycle);
  const uint32_t bgFill = cycleColor(cycle + 1u);

  strip_.fill(bgBase);

  // Exactly on a cycle boundary: background is already the "new color".
  if (inCycle == 0) return;

  // Fill bottom rows like a progress bar. Use ceiling so attempt 1 is visible.
  uint32_t filled = ((uint32_t)inCycle * (uint32_t)MATRIX_H + (uint32_t)steps - 1u) / (uint32_t)steps;
  if (filled > MATRIX_H) filled = MATRIX_H;

  // Optional subtle animation: a small moving highlight band across the filled
  // region so the meter reads as "alive".
  auto brightenTowardWhite = [&](uint32_t c, uint8_t alpha) -> uint32_t {
    if (alpha == 0) return c;
    uint8_t r = (uint8_t)((c >> 16) & 0xFF);
    uint8_t g = (uint8_t)((c >> 8) & 0xFF);
    uint8_t b = (uint8_t)(c & 0xFF);
    r = (uint8_t)(r + (uint16_t)(255 - r) * alpha / 255);
    g = (uint8_t)(g + (uint16_t)(255 - g) * alpha / 255);
    b = (uint8_t)(b + (uint16_t)(255 - b) * alpha / 255);
    return strip_.Color(r, g, b);
  };

  // Optional animation: sweep a subtle highlight band UP through the filled
  // region so it visually communicates "progress" (bottom -> top).
  int16_t hiRowFromBottom = -1;
  int16_t hiRowFromBottom2 = -1;
  uint32_t hiC = bgFill;
  uint32_t hiC2 = bgFill;

#if MAX_LEVEL_CHASE_PROGRESS_ANIM_ENABLED
  {
    const uint16_t speed = (uint16_t)MAX_LEVEL_CHASE_PROGRESS_ANIM_SPEED_MS;
    if (speed > 0 && filled > 0) {
      // Add a small "gap" at the end so the sweep restart isn't a harsh jump
      // on very small filled regions.
      const uint16_t gap = (filled >= 4) ? 2u : 0u;
      const uint16_t period = (uint16_t)filled + gap;
      const uint16_t pos = (uint16_t)((nowMs / speed) % (uint32_t)period);

      if (pos < filled) {
        hiRowFromBottom = (int16_t)pos;
        hiRowFromBottom2 = (pos > 0) ? (int16_t)(pos - 1) : -1;
      }

      hiC = brightenTowardWhite(bgFill, (uint8_t)MAX_LEVEL_CHASE_PROGRESS_ANIM_ALPHA);
      hiC2 = brightenTowardWhite(bgFill, (uint8_t)MAX_LEVEL_CHASE_PROGRESS_ANIM_TAIL_ALPHA);
    }
  }
#endif

  for (int16_t y = (int16_t)MATRIX_H - 1; y >= 0; y--) {
    const uint16_t rowFromBottomU = (uint16_t)((MATRIX_H - 1) - (uint16_t)y);
    if (rowFromBottomU >= filled) break;

    uint32_t rowC = bgFill;
    const int16_t rowFromBottom = (int16_t)rowFromBottomU;
    if (rowFromBottom == hiRowFromBottom) rowC = hiC;
    else if (rowFromBottom == hiRowFromBottom2) rowC = hiC2;

    for (uint8_t x = 0; x < MATRIX_W; x++) {
      setPixel((int16_t)x, y, rowC);
    }
  }
#else
  (void)chaseBg;
  (void)maxChaseAttempts;
  (void)nowMs;
  strip_.fill(baseBg);
#endif
}

bool MatrixDisplay::showMaxLevelNumberScreen(uint8_t value,
                                             uint32_t baseBg, uint32_t chaseBg,
                                             uint16_t maxChaseAttempts,
                                             uint32_t fg,
                                             uint32_t durationMs,
                                             AbortFn abortFn) {
  const uint8_t scale = 1;
  const uint32_t start = millis();

  // "AI level-up rollovers" == number of times the MAX chase progress meter
  // has fully filled and wrapped back to 0.
  //
  // Instead of drawing a number, render it as a tiny "LED counter":
  // - Fill pixels from bottom-left -> right, then up a row as needed.
  // - The NEXT pixel (upcoming rollover) gently pulses to hint progress,
  //   and the pulse speeds up slightly as it approaches the next rollover.
  uint16_t chaseSteps = (uint16_t)MAX_LEVEL_CHASE_PROGRESS_STEPS;
  if (chaseSteps < 1) chaseSteps = 1;

  const uint32_t aiRolloverCount = (uint32_t)maxChaseAttempts / (uint32_t)chaseSteps;
  const uint16_t aiInCycle = (uint16_t)(maxChaseAttempts % chaseSteps);

  // Keep the MAX digits readable by reserving a small band at the bottom.
  // (On a 16x16 matrix, 5 rows gives 80 pixels of "rollover history".)
  const uint8_t rolloverRows = (MATRIX_H < 5) ? (uint8_t)MATRIX_H : 5;
  const uint16_t rolloverPixels = (uint16_t)MATRIX_W * (uint16_t)rolloverRows;

  const uint16_t aiRolloverSolid = (aiRolloverCount >= (uint32_t)rolloverPixels)
                                    ? rolloverPixels
                                    : (uint16_t)aiRolloverCount;
  const bool aiRolloverFull = (aiRolloverCount >= (uint32_t)rolloverPixels);

  // Slightly dimmer than the main digits so the MAX number still reads as primary.
  const uint32_t aiRolloverSolidColor = dimColor(fg, 180);

  while ((millis() - start) < durationMs) {
    if (abortFn && abortFn()) return true;

    uint32_t now = millis();
    tickPowerBrightness(now);

    fillMaxChaseBackground(baseBg, chaseBg, maxChaseAttempts, now);
    drawNumberCenteredHalo(value, scale, fg,
                           (uint8_t)HIGH_SCREEN_HALO_ALPHA,
                           (bool)HIGH_SCREEN_HALO_DARKEN);

    // Bottom band: AI "level-up rollover" meter (LED counter).
    // Fills pixels from bottom-left -> right, then up a row as needed.
    //
    // Animation hint:
    // - If we're partway to the next rollover, the NEXT pixel pulses.
    // - If we've saturated the visible range, the LAST pixel pulses to imply "80+".
    for (uint16_t i = 0; i < aiRolloverSolid; i++) {
      int16_t x = (int16_t)(i % MATRIX_W);
      int16_t y = (int16_t)((MATRIX_H - 1) - (i / MATRIX_W));
      setPixel(x, y, aiRolloverSolidColor);
    }

    if (!aiRolloverFull && aiInCycle > 0 && chaseSteps > 1) {
      const uint16_t idx = aiRolloverSolid;
      int16_t x = (int16_t)(idx % MATRIX_W);
      int16_t y = (int16_t)((MATRIX_H - 1) - (idx / MATRIX_W));

      // Progress (0..255) toward the next rollover.
      const uint16_t denom = (chaseSteps > 1) ? (uint16_t)(chaseSteps - 1) : 1;
      const uint16_t inC = (aiInCycle > denom) ? denom : aiInCycle;
      const uint8_t prog = (uint8_t)((uint32_t)inC * 255u / denom);

      // Pulse period shrinks as we get closer to the rollover (subtle urgency).
      const uint16_t periodMax = 720;
      const uint16_t periodMin = 220;
      const uint16_t period = (uint16_t)(periodMax - (uint32_t)prog * (periodMax - periodMin) / 255u);

      const uint16_t t = (uint16_t)(now % (uint32_t)period);
      const uint16_t half = (period / 2u) ? (period / 2u) : 1u;
      const uint16_t tri = (t < half) ? t : (period - t);
      const uint8_t tri255 = (uint8_t)((uint32_t)tri * 255u / half);

      // Brightness pulse strength increases with progress.
      const uint16_t baseA = 18u + (uint16_t)prog * 28u / 255u;  // 18..46
      const uint16_t ampA  = 35u + (uint16_t)prog * 200u / 255u; // 35..235
      uint16_t a = baseA + (uint32_t)tri255 * ampA / 255u;
      if (a > 255u) a = 255u;

      setPixel(x, y, dimColor(fg, (uint8_t)a));
    } else if (aiRolloverFull && rolloverPixels > 0) {
      // Saturated: pulse the last pixel to imply "and more".
      const uint16_t idx = (uint16_t)(rolloverPixels - 1u);
      int16_t x = (int16_t)(idx % MATRIX_W);
      int16_t y = (int16_t)((MATRIX_H - 1) - (idx / MATRIX_W));

      const uint16_t period = 650;
      const uint16_t t = (uint16_t)(now % (uint32_t)period);
      const uint16_t half = period / 2u;
      const uint16_t tri = (t < half) ? t : (period - t);
      const uint8_t tri255 = (uint8_t)((uint32_t)tri * 255u / half);

      const uint8_t a = (uint8_t)(180u + (uint16_t)tri255 * 75u / 255u); // 180..255
      setPixel(x, y, dimColor(fg, a));
    }

    strip_.show();
    delay(25);
  }
  return false;
}

void MatrixDisplay::showBootLogo(uint32_t durationMs, AbortFn abortFn) {
  // Legacy splash; kept for optional use, but callers may skip it entirely.
  if (durationMs == 0) return;

  const uint32_t startMs = millis();
  while ((millis() - startMs) < durationMs) {
    if (abortFn && abortFn()) return;

    uint32_t now = millis();
    tickPowerBrightness(now);

    uint16_t phase = (uint16_t)((now / 8) & 0x01FF);
    if (phase > 255) phase = (uint16_t)(511 - phase);
    uint8_t v = (uint8_t)(60 + (phase * 170) / 255);

    uint32_t cText = strip_.Color(0, v, v);
    uint32_t cBlock = strip_.ColorHSV((uint16_t)(now * 40), 255, v);

    strip_.clear();

    drawTextCentered("TET", 1, cText);
    drawTextCentered("RIS", 10, cText);

    auto block2 = [&](int16_t x, int16_t y) {
      setPixel(x + 0, y + 0, cBlock);
      setPixel(x + 1, y + 0, cBlock);
      setPixel(x + 0, y + 1, cBlock);
      setPixel(x + 1, y + 1, cBlock);
    };

    block2(6, 4);
    block2(4, 6);
    block2(6, 6);
    block2(8, 6);

    setPixel(12, 7, strip_.Color(v, v, v));

    strip_.show();
    delay(25);
  }
}

// Super-clean tiny UI:
// - No score, no labels.
// - Only show level numbers, using background color to distinguish CURRENT vs MAX.
void MatrixDisplay::showGameOver(uint8_t level, uint8_t maxLevel, uint16_t maxChaseAttempts) {
  uint32_t fg = strip_.Color(140, 140, 140);
  // Base MAX background (purple) + the "chase" fill color.
  uint32_t bgMaxBase = dimColor(strip_.Color(170, 0, 170), 95);
  uint32_t bgMaxChase = dimColor(strip_.Color(0, 160, 160), 95);

  // If the run tied the MAX level, show it with the MAX styling (no need to repeat).
  if (level >= maxLevel) {
    (void)showMaxLevelNumberScreen(level, bgMaxBase, bgMaxChase, maxChaseAttempts, fg, 2200, nullptr);
    strip_.clear(); strip_.show(); delay(120);
    return;
  }

  // CURRENT level achieved (theme-derived background)
  uint32_t theme = pieceColor(level, 1);
  uint32_t bgCur = dimColor(theme, 55); // dim theme color
  (void)showLevelNumberScreen(level, bgCur, fg, 1400, nullptr);

  // MAX level achieved (persistent)
  (void)showMaxLevelNumberScreen(maxLevel, bgMaxBase, bgMaxChase, maxChaseAttempts, fg, 2000, nullptr);

  strip_.clear(); strip_.show(); delay(120);
}

void MatrixDisplay::showNewMaxLevel(uint8_t maxLevel) {
  // Celebration for a new MAX level:
  // 1) a short rainbow "party" background with the number overlaid
  // 2) settle on the clean MAX screen (purple background)
  const uint32_t partyMs = 1100;
  const uint32_t startMs = millis();

  while ((millis() - startMs) < partyMs) {
    uint32_t now = millis();
    tickPowerBrightness(now);

    for (uint8_t y = 0; y < MATRIX_H; y++) {
      for (uint8_t x = 0; x < MATRIX_W; x++) {
        uint16_t hue = (uint16_t)(
          (uint32_t)now * 55UL +
          (uint32_t)x * 5200UL +
          (uint32_t)y * 3100UL
        );

        // Slight checker to keep it textured on low-res.
        uint8_t v = (uint8_t)(((x ^ y) & 1) ? 80 : 120);
        strip_.setPixelColor(XY(x, y), strip_.ColorHSV(hue, 255, v));
      }
    }

    // Overlay the new max level as big white digits.
    drawNumberCentered(maxLevel, 2, strip_.Color(140, 140, 140));
    strip_.show();
    delay(25);
  }

  uint32_t fg = strip_.Color(140, 140, 140);
  uint32_t bg = dimColor(strip_.Color(170, 0, 170), 95);
  (void)showLevelNumberScreen(maxLevel, bg, fg, 2400, nullptr);

  strip_.clear(); strip_.show(); delay(120);
}

void MatrixDisplay::showBootStats(uint8_t maxLevel, uint16_t maxChaseAttempts, AbortFn abortFn) {
  // Boot: show MAX level only (number + background). Skippable.
  uint32_t fg = strip_.Color(140, 140, 140);
  uint32_t bgMaxBase = dimColor(strip_.Color(170, 0, 170), 95);
  uint32_t bgMaxChase = dimColor(strip_.Color(0, 160, 160), 95);

  (void)showMaxLevelNumberScreen(maxLevel, bgMaxBase, bgMaxChase, maxChaseAttempts, fg, 2400, abortFn);

  strip_.clear(); strip_.show();
  delay(120);
}


// ======================
// Boot intro: rapid falling pieces that fill the screen
// ======================

struct _IntroPt { int8_t x; int8_t y; };

static void _rotate4x4(_IntroPt& p) {
  // Rotate within a 4x4 cell (x,y) -> (y, 3-x)
  const int8_t x = p.x;
  p.x = p.y;
  p.y = (int8_t)(3 - x);
}

static void _normalizePts(_IntroPt pts[4], int8_t& w, int8_t& h) {
  int8_t minx =  127, miny =  127;
  int8_t maxx = -127, maxy = -127;
  for (int i = 0; i < 4; i++) {
    if (pts[i].x < minx) minx = pts[i].x;
    if (pts[i].y < miny) miny = pts[i].y;
    if (pts[i].x > maxx) maxx = pts[i].x;
    if (pts[i].y > maxy) maxy = pts[i].y;
  }
  for (int i = 0; i < 4; i++) {
    pts[i].x = (int8_t)(pts[i].x - minx);
    pts[i].y = (int8_t)(pts[i].y - miny);
  }
  w = (int8_t)(maxx - minx + 1);
  h = (int8_t)(maxy - miny + 1);
}

// Base shapes in a 4x4 grid (rotation 0). 7 tetrominoes.
static const _IntroPt _BASE[7][4] = {
  // I
  {{0,1},{1,1},{2,1},{3,1}},
  // O
  {{1,1},{2,1},{1,2},{2,2}},
  // T
  {{1,1},{0,2},{1,2},{2,2}},
  // S
  {{1,1},{2,1},{0,2},{1,2}},
  // Z
  {{0,1},{1,1},{1,2},{2,2}},
  // J
  {{0,1},{0,2},{1,2},{2,2}},
  // L
  {{2,1},{0,2},{1,2},{2,2}},
};

static uint32_t _introColor(Adafruit_NeoPixel& strip, uint8_t id) {
  // Simple bold palette (not tied to gameplay level/theme).
  switch (id % 7) {
    case 0: return strip.Color(0, 180, 255); // cyan
    case 1: return strip.Color(255, 220, 0); // yellow
    case 2: return strip.Color(180, 0, 255); // purple
    case 3: return strip.Color(0, 255, 0);   // green
    case 4: return strip.Color(255, 0, 0);   // red
    case 5: return strip.Color(0, 80, 255);  // blue
    default:return strip.Color(255, 140, 0); // orange
  }
}

void MatrixDisplay::showIntroDropFill(uint32_t maxDurationMs, AbortFn abortFn) {
#if !INTRO_ENABLED
  (void)maxDurationMs;
  (void)abortFn;
  return;
#else
  const uint32_t start = millis();
  const uint32_t deadline = start + maxDurationMs;

  // Storage for filled cells
  bool filled[MATRIX_H][MATRIX_W] = {};
  uint32_t col[MATRIX_H][MATRIX_W] = {};

  int filledCount = 0;

  auto rampU16 = [&](uint16_t a, uint16_t b, uint16_t p1024) -> uint16_t {
    if (p1024 >= 1024) return b;
    const int32_t delta = (int32_t)b - (int32_t)a;
    const int32_t v = (int32_t)a + (delta * (int32_t)p1024) / 1024;
    if (v < 0) return 0;
    if (v > 65535) return 65535;
    return (uint16_t)v;
  };

  auto progress1024 = [&]() -> uint16_t {
    const uint32_t now = (uint32_t)millis();
    const uint32_t elapsed = (now - start);
    const uint32_t total = (uint32_t)(MATRIX_W * MATRIX_H);
    const uint32_t fillP = (total == 0) ? 0U : (uint32_t)filledCount * 1024U / total;
    const uint32_t timeP = (maxDurationMs == 0) ? 1024U : (elapsed * 1024U / maxDurationMs);
    uint32_t p = (fillP > timeP) ? fillP : timeP;
    if (p > 1024U) p = 1024U;
    return (uint16_t)p;
  };

  auto frameDelayMs = [&]() -> uint16_t {
    const uint16_t p = progress1024();
    const uint16_t d = rampU16((uint16_t)INTRO_FRAME_MS_START, (uint16_t)INTRO_FRAME_MS_END, p);
    return (d < 1) ? 1 : d;
  };

  auto framesPerPiece = [&]() -> uint8_t {
    const uint16_t p = progress1024();
    const uint16_t f = rampU16((uint16_t)INTRO_FRAMES_PER_PIECE_START, (uint16_t)INTRO_FRAMES_PER_PIECE_END, p);
    return (f < 2) ? 2 : (uint8_t)f;
  };


  auto collides = [&](int x, int y, const _IntroPt pts[4]) -> bool {
    for (int i = 0; i < 4; i++) {
      const int xx = x + pts[i].x;
      const int yy = y + pts[i].y;
      if (xx < 0 || xx >= MATRIX_W) return true;
      if (yy >= MATRIX_H) return true;
      if (yy >= 0 && filled[yy][xx]) return true;
    }
    return false;
  };

  auto drawScene = [&](int px, int py, const _IntroPt pts[4], uint32_t pc, bool drawPiece) {
    strip_.clear();

    // draw stacked blocks
    for (int y = 0; y < MATRIX_H; y++) {
      for (int x = 0; x < MATRIX_W; x++) {
        if (filled[y][x]) {
          strip_.setPixelColor(XY((uint8_t)x, (uint8_t)y), col[y][x]);
        }
      }
    }

    // draw falling piece
    if (drawPiece) {
      for (int i = 0; i < 4; i++) {
        const int xx = px + pts[i].x;
        const int yy = py + pts[i].y;
        if (xx < 0 || xx >= MATRIX_W || yy < 0 || yy >= MATRIX_H) continue;
        strip_.setPixelColor(XY((uint8_t)xx, (uint8_t)yy), pc);
      }
    }

    strip_.show();
  };

  // Seed RNG for Arduino's random(). On ESP32 we can mix in the hardware RNG
  // so the intro pattern isn't repeatable from boot-to-boot.
  #if defined(ARDUINO_ARCH_ESP32)
    randomSeed((uint32_t)(esp_random() ^ (uint32_t)micros() ^ ((uint32_t)millis() << 16)));
  #else
    randomSeed((uint32_t)((uint32_t)micros() ^ ((uint32_t)millis() << 16)));
  #endif

  // Drop pieces until filled or time.
  while (millis() < deadline && filledCount < (MATRIX_W * MATRIX_H)) {
    // Build a random tetromino (type + rotation), normalized to top-left.
    const uint8_t type = (uint8_t)random(0, 7);
    const uint8_t rot = (uint8_t)random(0, 4);

    _IntroPt pts[4];
    for (int i = 0; i < 4; i++) pts[i] = _BASE[type][i];
    for (int r = 0; r < rot; r++) for (int i = 0; i < 4; i++) _rotate4x4(pts[i]);

    int8_t w = 0, h = 0;
    _normalizePts(pts, w, h);
    if (w <= 0 || h <= 0) continue;

    // Choose an x that fits.
    const int x = (int)random(0, MATRIX_W - w + 1);

    // Compute landing y by stepping down until collision.
    int y = -h;
    while (!collides(x, y + 1, pts)) y++;

    // If we cannot place (collision even at start), bail to a fast fill.
    if (collides(x, y, pts)) break;

    // Animate the drop in a bounded number of frames.
    const int yStart = -h;
    const int yEnd = y;
    const int dy = yEnd - yStart;
    const int frames = (int)framesPerPiece();
const int step = (dy <= 0) ? 1 : max(1, dy / (frames - 1));

    const uint32_t pc = _introColor(strip_, type);

    for (int yy = yStart; yy < yEnd; yy += step) {
      tickPowerBrightness(millis());
      drawScene(x, yy, pts, pc, true);
      if (abortFn && abortFn()) return;
      delay((uint32_t)frameDelayMs());
    }

    // Final landing frame
    tickPowerBrightness(millis());
    drawScene(x, yEnd, pts, pc, true);
    if (abortFn && abortFn()) return;
    delay((uint32_t)frameDelayMs());

    // Merge into filled buffer
    for (int i = 0; i < 4; i++) {
      const int xx = x + pts[i].x;
      const int yy = yEnd + pts[i].y;
      if (xx < 0 || xx >= MATRIX_W || yy < 0 || yy >= MATRIX_H) continue;
      if (!filled[yy][xx]) {
        filled[yy][xx] = true;
        col[yy][xx] = pc;
        filledCount++;
      }
    }
  }

  // If we're not full yet (due to placement dead-end), finish with a quick block "rain".
  if (filledCount < (MATRIX_W * MATRIX_H) && millis() < deadline) {
    for (int y = MATRIX_H - 1; y >= 0; y--) {
      for (int x = 0; x < MATRIX_W; x++) {
        if (filled[y][x]) continue;
        filled[y][x] = true;
        col[y][x] = strip_.Color(80, 80, 80);
        filledCount++;

        tickPowerBrightness(millis());

        // Draw updated scene (no falling piece)
        strip_.clear();
        for (int yy = 0; yy < MATRIX_H; yy++) {
          for (int xx = 0; xx < MATRIX_W; xx++) {
            if (filled[yy][xx]) strip_.setPixelColor(XY((uint8_t)xx, (uint8_t)yy), col[yy][xx]);
          }
        }
        strip_.show();

        if (abortFn && abortFn()) return;
        delay((uint32_t)max((int)1, (int)rampU16(5, 1, progress1024())));
        if (millis() >= deadline) break;
      }
      if (millis() >= deadline) break;
    }
  }

  // Brief hold on the filled screen
  const uint32_t nowHold = (uint32_t)millis();
  const uint32_t holdEnd = (deadline < (nowHold + 180U)) ? deadline : (nowHold + 180U);
while (millis() < holdEnd) {
    tickPowerBrightness(millis());
    if (abortFn && abortFn()) return;
    delay(10);
  }
  // End with a "bang": a few bright flashes + a quick expanding ring.
  for (int i = 0; i < (int)INTRO_BANG_FLASHES; i++) {
    tickPowerBrightness(millis());
    // Flash white
    strip_.fill(strip_.Color(255, 255, 255));
    strip_.show();
    if (abortFn && abortFn()) return;
    delay((uint32_t)INTRO_BANG_FLASH_MS);

    tickPowerBrightness(millis());
    // Back to the filled scene (repaint)
    strip_.clear();
    for (int yy = 0; yy < MATRIX_H; yy++) {
      for (int xx = 0; xx < MATRIX_W; xx++) {
        if (filled[yy][xx]) strip_.setPixelColor(XY((uint8_t)xx, (uint8_t)yy), col[yy][xx]);
      }
    }
    strip_.show();
    if (abortFn && abortFn()) return;
    delay((uint32_t)INTRO_BANG_FLASH_MS);
  }

  // Expanding ring from center
  const int cx = MATRIX_W / 2;
  const int cy = MATRIX_H / 2;
  const int maxR = (MATRIX_W > MATRIX_H) ? MATRIX_W : MATRIX_H;
  for (int r = 0; r < maxR; r++) {
    tickPowerBrightness(millis());
    strip_.clear();
    for (int y = 0; y < MATRIX_H; y++) {
      for (int x = 0; x < MATRIX_W; x++) {
        const int d = abs(x - cx) + abs(y - cy);
        if (d == r) {
          strip_.setPixelColor(XY((uint8_t)x, (uint8_t)y), strip_.Color(255, 255, 255));
        } else if (filled[y][x]) {
          // Dim background of filled pieces for contrast
          uint32_t c = col[y][x];
          uint8_t rr = (c >> 16) & 0xFF;
          uint8_t gg = (c >> 8) & 0xFF;
          uint8_t bb = (c) & 0xFF;
          strip_.setPixelColor(XY((uint8_t)x, (uint8_t)y), strip_.Color(rr / 5, gg / 5, bb / 5));
        }
      }
    }
    strip_.show();
    if (abortFn && abortFn()) return;
    delay((uint32_t)INTRO_BANG_RING_MS);
  }

  // Clean exit to game
  tickPowerBrightness(millis());
  strip_.clear();
  strip_.show();

#endif
}


// ======================
// Boot intro: scrolling "TETRIS" (big readable letters)
// ======================
// Minimal 5x7 font for letters we need (T,E,R,I,S, space).
static bool _glyph5x7(char c, uint8_t rows[7]) {
  for (int i = 0; i < 7; i++) rows[i] = 0;
  switch (c) {
    case 'T': rows[0]=0b11111; rows[1]=0b00100; rows[2]=0b00100; rows[3]=0b00100; rows[4]=0b00100; rows[5]=0b00100; rows[6]=0b00100; return true;
    case 'E': rows[0]=0b11111; rows[1]=0b10000; rows[2]=0b10000; rows[3]=0b11110; rows[4]=0b10000; rows[5]=0b10000; rows[6]=0b11111; return true;
    case 'R': rows[0]=0b11110; rows[1]=0b10001; rows[2]=0b10001; rows[3]=0b11110; rows[4]=0b10100; rows[5]=0b10010; rows[6]=0b10001; return true;
    case 'I': rows[0]=0b11111; rows[1]=0b00100; rows[2]=0b00100; rows[3]=0b00100; rows[4]=0b00100; rows[5]=0b00100; rows[6]=0b11111; return true;
    case 'S': rows[0]=0b01111; rows[1]=0b10000; rows[2]=0b10000; rows[3]=0b01110; rows[4]=0b00001; rows[5]=0b00001; rows[6]=0b11110; return true;
    case ' ': default: return true;
  }
}

static inline uint16_t _rampU16(uint16_t a, uint16_t b, uint16_t p1024) {
  if (p1024 >= 1024) return b;
  const int32_t delta = (int32_t)b - (int32_t)a;
  const int32_t v = (int32_t)a + (delta * (int32_t)p1024) / 1024;
  if (v < 0) return 0;
  if (v > 65535) return 65535;
  return (uint16_t)v;
}

void MatrixDisplay::showIntroMarquee(const char* text, uint8_t scale, uint32_t maxDurationMs, AbortFn abortFn) {
#if !INTRO_ENABLED
  (void)text; (void)scale; (void)maxDurationMs; (void)abortFn;
  return;
#else
  if (!text) return;
  if (scale < 1) scale = 1;

  const uint32_t start = millis();
  const uint32_t deadline = start + maxDurationMs;

  // Compute text pixel width (5 glyph + 1 space) * scale per char.
  int len = 0;
  while (text[len] != '\0' && len < 64) len++;

  const int charW = (5 + 1) * (int)scale;
  const int textW = len * charW;

  // We'll scroll from right edge to left beyond full text.
  int xStart = MATRIX_W;
  int xEnd = -textW;

  auto drawScaledGlyph = [&](char c, int x0, int y0, uint32_t color) {
    uint8_t rows[7];
    _glyph5x7(c, rows);
    for (int ry = 0; ry < 7; ry++) {
      for (int rx = 0; rx < 5; rx++) {
        if ((rows[ry] >> (4 - rx)) & 0x1) {
          // scale pixel to block
          for (int sy = 0; sy < (int)scale; sy++) {
            for (int sx = 0; sx < (int)scale; sx++) {
              setPixel((int16_t)(x0 + rx*scale + sx), (int16_t)(y0 + ry*scale + sy), color);
            }
          }
        }
      }
    }
  };

  auto drawTextAt = [&](int x, uint32_t color) {
    strip_.clear();
    // center vertically; 7*scale fits within 16 (scale=2 => 14px)
    const int y0 = (MATRIX_H - (7 * (int)scale)) / 2;
    int cx = x;
    for (int i = 0; i < len; i++) {
      drawScaledGlyph(text[i], cx, y0, color);
      cx += charW;
    }
    strip_.show();
  };

  const uint32_t white = strip_.Color(255, 255, 255);

  // Track the last drawn X so we can end smoothly without "jumping" to a
  // centered pose (which looks like a restart on small matrices).
  int lastX = xStart;

  const int totalSteps = (xStart - xEnd) + 1;
  for (int step = 0; step < totalSteps; step++) {
    tickPowerBrightness(millis());
    const int x = xStart - step;
    lastX = x;
    drawTextAt(x, white);

    if (abortFn && abortFn()) return;
    if (millis() >= deadline) break;

    const uint16_t p = (totalSteps <= 1) ? 1024 : (uint16_t)((uint32_t)step * 1024U / (uint32_t)(totalSteps - 1));
    const uint16_t d = _rampU16((uint16_t)INTRO_MARQUEE_SPEED_MS_START, (uint16_t)INTRO_MARQUEE_SPEED_MS_END, p);
    delay((uint32_t)(d < 1 ? 1 : d));
  }

  // Smooth outro: brief hold on the *current* frame (no reposition/jump), then
  // a short fade to black so the next intro phase doesn't feel abrupt.
  {
    const uint32_t nowHold = (uint32_t)millis();
    const uint32_t holdEnd = (deadline < (nowHold + (uint32_t)INTRO_MARQUEE_END_HOLD_MS))
                               ? deadline
                               : (nowHold + (uint32_t)INTRO_MARQUEE_END_HOLD_MS);
    while (millis() < holdEnd) {
      tickPowerBrightness(millis());
      if (abortFn && abortFn()) return;
      delay(10);
    }
  }

  // Quick fade-out (best-effort within remaining time).
  for (uint8_t i = 0; i < (uint8_t)INTRO_MARQUEE_FADE_FRAMES; i++) {
    if (abortFn && abortFn()) return;
    if (millis() >= deadline) break;
    tickPowerBrightness(millis());
    const uint8_t denom = (INTRO_MARQUEE_FADE_FRAMES <= 1) ? 1U : (uint8_t)(INTRO_MARQUEE_FADE_FRAMES - 1);
    const uint8_t a = (uint8_t)(255 - (uint16_t)i * 255U / (uint16_t)denom);
    drawTextAt(lastX, scaleColor(white, a));
    delay((uint32_t)INTRO_MARQUEE_FADE_FRAME_MS);
  }

  tickPowerBrightness(millis());
  strip_.clear();
  strip_.show();
#endif
}
