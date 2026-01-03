#include "display_matrix.h"
#include <Arduino.h>

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

void MatrixDisplay::drawNumberCentered(uint8_t value, uint8_t scale, uint32_t color) {
  if (value > 99) value = 99;

  uint8_t d0 = (uint8_t)(value / 10);
  uint8_t d1 = (uint8_t)(value % 10);
  const bool twoDigits = (value >= 10);

  const int digitW = 5 * scale;
  const int digitH = 7 * scale;
  const int spacing = scale; // 1*scale spacing

  const int totalW = twoDigits ? (digitW * 2 + spacing) : digitW;

  int16_t startX = (int16_t)((MATRIX_W - totalW) / 2);
  int16_t startY = (int16_t)((MATRIX_H - digitH) / 2);

  if (startX < 0) startX = 0;
  if (startY < 0) startY = 0;

  if (!twoDigits) {
    drawDigit5x7Scaled(d1, startX, startY, scale, color);
  } else {
    drawDigit5x7Scaled(d0, startX, startY, scale, color);
    drawDigit5x7Scaled(d1, (int16_t)(startX + digitW + spacing), startY, scale, color);
  }

}

bool MatrixDisplay::showLevelNumberScreen(uint8_t value, uint32_t bg, uint32_t fg, uint32_t durationMs, AbortFn abortFn) {
  const uint8_t scale = 1; // 1px-stroke digits (avoid chunky "block" look)
  const uint32_t start = millis();

  while ((millis() - start) < durationMs) {
    if (abortFn && abortFn()) return true;

    strip_.fill(bg);
    drawNumberCentered(value, scale, fg);
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
void MatrixDisplay::showGameOver(uint8_t level, uint8_t maxLevel) {
  uint32_t fg = strip_.Color(140, 140, 140);

  // If the run tied the MAX level, show it with the MAX styling (no need to repeat).
  if (level >= maxLevel) {
    uint32_t bgMax = dimColor(strip_.Color(170, 0, 170), 95);
    (void)showLevelNumberScreen(level, bgMax, fg, 2200, nullptr);
    strip_.clear(); strip_.show(); delay(120);
    return;
  }

  // CURRENT level achieved (theme-derived background)
  uint32_t theme = pieceColor(level, 1);
  uint32_t bgCur = dimColor(theme, 55); // dim theme color
  (void)showLevelNumberScreen(level, bgCur, fg, 1400, nullptr);

  // MAX level achieved (persistent)
  uint32_t bgMax = dimColor(strip_.Color(170, 0, 170), 95);
  (void)showLevelNumberScreen(maxLevel, bgMax, fg, 2000, nullptr);

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

void MatrixDisplay::showBootStats(uint8_t maxLevel, AbortFn abortFn) {
  // Boot: show MAX level only (number + background). Skippable.
  uint32_t fg = strip_.Color(140, 140, 140);
  uint32_t bg = dimColor(strip_.Color(170, 0, 170), 95);

  (void)showLevelNumberScreen(maxLevel, bg, fg, 2400, abortFn);

  strip_.clear(); strip_.show();
  delay(120);
}
