#include "display_matrix.h"
#include <Arduino.h>


// Tiny 3x5 Font
static const uint8_t TINY_FONT[][3] = {
  {0x1F, 0x11, 0x1F}, // 0
  {0x00, 0x1F, 0x00}, // 1
  {0x1D, 0x15, 0x17}, // 2
  {0x15, 0x15, 0x1F}, // 3
  {0x07, 0x04, 0x1F}, // 4
  {0x17, 0x15, 0x1D}, // 5
  {0x1F, 0x15, 0x1D}, // 6
  {0x01, 0x01, 0x1F}, // 7
  {0x1F, 0x15, 0x1F}, // 8
  {0x17, 0x15, 0x1F}, // 9
  {0x00, 0x00, 0x00}, // : (handled manually)
  {0x00, 0x00, 0x00}, // Space
  {0x1F, 0x05, 0x1F}, // A
  {0x1F, 0x15, 0x0A}, // B
  {0x0E, 0x11, 0x11}, // C
  {0x1F, 0x11, 0x0E}, // D
  {0x1F, 0x15, 0x15}, // E
  {0x1F, 0x05, 0x01}, // F
  {0x0E, 0x11, 0x1D}, // G
  {0x1F, 0x04, 0x1F}, // H
  {0x11, 0x1F, 0x11}, // I
  {0x08, 0x10, 0x0F}, // J
  {0x1F, 0x04, 0x1B}, // K
  {0x1F, 0x10, 0x10}, // L
  {0x1F, 0x02, 0x1F}, // M
  {0x1F, 0x02, 0x1C}, // N
  {0x0E, 0x11, 0x0E}, // O
  {0x1F, 0x05, 0x02}, // P
  {0x0E, 0x13, 0x0E}, // Q
  {0x1F, 0x05, 0x1A}, // R
  {0x12, 0x15, 0x09}, // S
  {0x01, 0x1F, 0x01}, // T
  {0x1F, 0x10, 0x1F}, // U
  {0x07, 0x18, 0x07}, // V
  {0x1F, 0x08, 0x1F}, // W
  {0x1B, 0x04, 0x1B}, // X
  {0x03, 0x1C, 0x03}, // Y
  {0x19, 0x15, 0x13}  // Z
};

void MatrixDisplay::drawChar(int16_t x, int16_t y, char c, uint32_t color) {
  int idx = -1;
  if (c >= '0' && c <= '9') idx = c - '0';
  else if (c >= 'A' && c <= 'Z') idx = 12 + (c - 'A');
  else if (c >= 'a' && c <= 'z') idx = 12 + (c - 'a');
  else if (c == ':') idx = 10;

  if (idx == -1) return;

  if (idx == 10) {
    setPixel(x + 1, y + 1, color);
    setPixel(x + 1, y + 3, color);
    return;
  }

  for (int col = 0; col < 3; col++) {
    uint8_t bits = TINY_FONT[idx][col];
    for (int row = 0; row < 5; row++) {
      if ((bits >> row) & 1) {
        setPixel(x + col, y + row, color);
      }
    }
  }
}


void MatrixDisplay::drawTextCentered(String text, int16_t y, uint32_t color) {
  int totalWidth = text.length() * 4 - 1;
  int startX = (MATRIX_W - totalWidth) / 2;
  if (startX < 0) startX = 0;

  for (int i = 0; i < (int)text.length(); i++) {
    drawChar(startX + (i * 4), y, text[i], color);
  }
}


void MatrixDisplay::drawText(int16_t x, int16_t y, const String& text, uint32_t color) {
  for (int i = 0; i < (int)text.length(); i++) {
    drawChar(x + (i * 4), y, text[i], color);
  }
}


bool MatrixDisplay::showTwoLineTitleValue(const String& title, const String& value,
                                         uint32_t titleColor, uint32_t valueColor,
                                         uint32_t durationMs,
                                         AbortFn abortFn) {
  const int16_t yTitle = 1;
  const int16_t yValue = 10;

  const int16_t valueW = (int16_t)((int)value.length() * 4 - 1);

  // Animation tuning
  const uint16_t pxMs = 320;     // ms per pixel scroll speed
  const uint8_t overshoot = 1;
  const uint32_t startHoldMs = 800;
  const uint32_t edgeHoldMs  = 800;
  const uint32_t endHoldMs   = 1200;

  if (valueW <= MATRIX_W) {
    const uint32_t startMs = millis();
    while ((millis() - startMs) < durationMs) {
      if (abortFn && abortFn()) return true;
      strip_.clear();
      drawTextCentered(title, yTitle, titleColor);
      drawTextCentered(value, yValue, valueColor);
      strip_.show();
      delay(25);
    }
    return false;
  }

  const int16_t maxShift = valueW - MATRIX_W;
  const int16_t leftEdge  = -(overshoot);
  const int16_t rightEdge = maxShift + overshoot;

  const uint32_t oneWayMs = (uint32_t)(rightEdge - leftEdge) * pxMs;
  const uint32_t cycleMs = (2 * oneWayMs) + (2 * edgeHoldMs);
  const uint32_t startMs = millis();

  uint32_t runMs = durationMs;
  if (runMs < (cycleMs + startHoldMs + endHoldMs))
    runMs = cycleMs + startHoldMs + endHoldMs;

  while ((millis() - startMs) < runMs) {
    if (abortFn && abortFn()) return true;

    uint32_t elapsed = millis() - startMs;

    int16_t shift = 0;

    if (elapsed < startHoldMs) {
      shift = 0;
    } else {
      uint32_t t = (elapsed - startHoldMs) % cycleMs;

      if (t < edgeHoldMs) {
        shift = leftEdge;
      } else if (t < edgeHoldMs + oneWayMs) {
        shift = leftEdge + (int16_t)((t - edgeHoldMs) / (float)pxMs);
      } else if (t < edgeHoldMs + oneWayMs + edgeHoldMs) {
        shift = rightEdge;
      } else {
        shift = rightEdge - (int16_t)((t - edgeHoldMs - oneWayMs - edgeHoldMs) / (float)pxMs);
      }
    }

    strip_.clear();
    drawTextCentered(title, yTitle, titleColor);
    drawText(-shift, yValue, value, valueColor);
    strip_.show();
    delay(25);
  }

  return false;
}

