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

struct NumberLayout {
  bool twoDigits;
  uint8_t s;
  uint8_t d0;
  uint8_t d1;
  int16_t x;
  int16_t y;
  int digitW;
  int digitH;
  int spacing;
  int totalW;
};

static NumberLayout layoutNumberCentered(uint8_t value, uint8_t scaleWanted) {
  if (value > 99) value = 99;

  NumberLayout L{};
  L.twoDigits = (value >= 10);
  L.d0 = (uint8_t)(value / 10);
  L.d1 = (uint8_t)(value % 10);

  const int16_t regionW = MATRIX_W;
  const int16_t regionH = (BOARD_H < MATRIX_H) ? BOARD_H : MATRIX_H;

  // Auto-shrink if the requested scale does not fit.
  uint8_t s = scaleWanted;
  while (s > 1) {
    const int digitW = 5 * s;
    const int digitH = 7 * s;
    const int spacing = s; // 1*scale spacing
    const int totalW = L.twoDigits ? (digitW * 2 + spacing) : digitW;
    if (totalW <= regionW && digitH <= regionH) break;
    s--;
  }

  L.s = s;
  L.digitW = 5 * s;
  L.digitH = 7 * s;
  L.spacing = s;
  L.totalW = L.twoDigits ? (L.digitW * 2 + L.spacing) : L.digitW;

  L.x = (int16_t)((regionW - L.totalW) / 2);
  L.y = (int16_t)((regionH - L.digitH) / 2);
  if (L.x < 0) L.x = 0;
  if (L.y < 0) L.y = 0;

  return L;
}


uint32_t MatrixDisplay::dimColor(uint32_t c, uint8_t alpha) const {
  uint8_t r = (c >> 16) & 0xFF;
  uint8_t g = (c >> 8) & 0xFF;
  uint8_t b = (c >> 0) & 0xFF;
  r = (uint8_t)((uint16_t)r * alpha / 255);
  g = (uint8_t)((uint16_t)g * alpha / 255);
  b = (uint8_t)((uint16_t)b * alpha / 255);
  return strip_.Color(r, g, b);
}

void MatrixDisplay::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint32_t c) {
  if (w <= 0 || h <= 0) return;
  for (int16_t yy = y; yy < (int16_t)(y + h); yy++) {
    for (int16_t xx = x; xx < (int16_t)(x + w); xx++) {
      setPixel(xx, yy, c);
    }
  }
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
  NumberLayout L = layoutNumberCentered(value, scale);

  if (!L.twoDigits) {
    drawDigit5x7Scaled(L.d1, L.x, L.y, L.s, color);
  } else {
    drawDigit5x7Scaled(L.d0, L.x, L.y, L.s, color);
    drawDigit5x7Scaled(L.d1, (int16_t)(L.x + L.digitW + L.spacing), L.y, L.s, color);
  }
}

void MatrixDisplay::drawNumberCenteredStyled(uint8_t value, uint8_t scale,
                                            uint32_t fg, uint32_t outline,
                                            uint32_t plate, uint8_t pad) {
  NumberLayout L = layoutNumberCentered(value, scale);

  // Backplate behind the digits to keep them readable on busy backgrounds.
  if (pad > 0) {
    fillRect((int16_t)(L.x - pad), (int16_t)(L.y - pad),
             (int16_t)(L.totalW + (int)pad * 2), (int16_t)(L.digitH + (int)pad * 2),
             plate);
  }

  auto drawDigitsAt = [&](int16_t x, int16_t y, uint32_t c) {
    if (!L.twoDigits) {
      drawDigit5x7Scaled(L.d1, x, y, L.s, c);
    } else {
      drawDigit5x7Scaled(L.d0, x, y, L.s, c);
      drawDigit5x7Scaled(L.d1, (int16_t)(x + L.digitW + L.spacing), y, L.s, c);
    }
  };

  // Crisp 1px outline (4-neighbor) for contrast.
  drawDigitsAt((int16_t)(L.x - 1), L.y, outline);
  drawDigitsAt((int16_t)(L.x + 1), L.y, outline);
  drawDigitsAt(L.x, (int16_t)(L.y - 1), outline);
  drawDigitsAt(L.x, (int16_t)(L.y + 1), outline);

  // Foreground digits.
  drawDigitsAt(L.x, L.y, fg);
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

bool MatrixDisplay::showHighScoreNumberScreen(uint8_t value, uint32_t bg, uint32_t durationMs, AbortFn abortFn) {
  // High-contrast digits (max brightness at the configured BRIGHTNESS), with a subtle plate.
  const uint32_t fg = strip_.Color(255, 255, 255);
  const uint32_t outline = strip_.Color(0, 0, 0);
  const uint32_t plate = dimColor(bg, 110); // darker than bg, but not a hard black box
  const uint8_t pad = 1;

  const uint32_t start = millis();
  while ((millis() - start) < durationMs) {
    if (abortFn && abortFn()) return true;

    strip_.fill(bg);
    drawNumberCenteredStyled(value, 2, fg, outline, plate, pad);
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
  // Game-over screens should be readable at a glance.
  // (This does NOT affect the in-game level "dropdown" overlay; that's rendered in display_matrix_render.cpp.)
  uint32_t bgMax = dimColor(strip_.Color(170, 0, 170), 95);

  // If the run tied the MAX level, show it with the MAX styling (no need to repeat).
  if (level >= maxLevel) {
    (void)showHighScoreNumberScreen(level, bgMax, 2200, nullptr);
    strip_.clear(); strip_.show(); delay(120);
    return;
  }

  // CURRENT level achieved (theme-derived background)
  uint32_t theme = pieceColor(level, 1);
  uint32_t bgCur = dimColor(theme, 55); // dim theme color
  (void)showHighScoreNumberScreen(level, bgCur, 1400, nullptr);

  // MAX level achieved (persistent)
  (void)showHighScoreNumberScreen(maxLevel, bgMax, 2000, nullptr);

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

    // Overlay the new max level with a small dark plate + outline so it's readable.
    drawNumberCenteredStyled(maxLevel, 2,
                             strip_.Color(255, 255, 255),
                             strip_.Color(0, 0, 0),
                             strip_.Color(0, 0, 0),
                             1);
    strip_.show();
    delay(25);
  }

  uint32_t bg = dimColor(strip_.Color(170, 0, 170), 95);
  (void)showHighScoreNumberScreen(maxLevel, bg, 2400, nullptr);

  strip_.clear(); strip_.show(); delay(120);
}

void MatrixDisplay::showBootStats(uint8_t maxLevel, AbortFn abortFn) {
  // Boot: show MAX level only (number + background). Skippable.
  uint32_t bg = dimColor(strip_.Color(170, 0, 170), 95);
  (void)showHighScoreNumberScreen(maxLevel, bg, 2400, abortFn);

  strip_.clear(); strip_.show();
  delay(120);
}
