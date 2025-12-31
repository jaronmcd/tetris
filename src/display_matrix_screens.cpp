#include "display_matrix.h"
#include <Arduino.h>


void MatrixDisplay::showBootLogo(uint32_t durationMs, AbortFn abortFn) {
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


void MatrixDisplay::showGameOver(uint32_t score, uint8_t level) {
  uint32_t cLevelLabel = strip_.Color(0, 0, 255);
  uint32_t cLevelVal   = strip_.Color(255, 255, 255);
  uint32_t cScoreLabel = strip_.Color(0, 255, 0);
  uint32_t cScoreVal   = strip_.Color(255, 255, 255);

  (void)showTwoLineTitleValue("LVL", String(level), cLevelLabel, cLevelVal, 3000, nullptr);
  (void)showTwoLineTitleValue("SCR", String(score), cScoreLabel, cScoreVal, 8000, nullptr);

  strip_.clear(); strip_.show(); delay(250);
}

void MatrixDisplay::showNewMaxLevel(uint8_t maxLevel) {
  for (int i = 0; i < 6; i++) {
    strip_.fill(strip_.Color(150, 0, 0)); strip_.show(); delay(40);
    strip_.fill(strip_.Color(0, 150, 0)); strip_.show(); delay(40);
    strip_.fill(strip_.Color(0, 0, 150)); strip_.show(); delay(40);
  }
  strip_.clear();
  strip_.show();

  uint32_t cNew = strip_.Color(255, 0, 255);
  uint32_t cVal = strip_.Color(255, 255, 255);

  // Keep it simple on the tiny display: celebrate the new MAX level.
  (void)showTwoLineTitleValue("MAX", String(maxLevel), cNew, cVal, 9000, nullptr);

  strip_.clear(); strip_.show(); delay(250);
}


void MatrixDisplay::showBootStats(uint8_t maxLevel, AbortFn abortFn) {
  uint32_t cMaxLabel  = strip_.Color(255, 0, 255);
  uint32_t cMaxVal    = strip_.Color(255, 255, 255);

  (void)showTwoLineTitleValue("MAX", String(maxLevel), cMaxLabel, cMaxVal, 5000, abortFn);

  strip_.clear(); strip_.show();
  delay(200);
}

