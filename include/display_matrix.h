#pragma once
#include <Arduino.h>
#include <stdint.h>
#include <Adafruit_NeoPixel.h>

#include "config.h"
#include "tetris.h"

class MatrixDisplay {
public:
  using AbortFn = bool (*)();

  MatrixDisplay();

  void begin();
  void bootFlash();
  void levelUpFlash(uint8_t nextLevel);

  // Optional: Boot splash/logo screen (skippable)
  void showBootLogo(uint32_t durationMs, AbortFn abortFn = nullptr);

  // Normal Game Over (Score + Level)
  void showGameOver(uint32_t score, uint8_t level);

  // Celebration for beating the record
  void showNewHighScore(uint32_t score);

  // Boot: shows HI + score, then LVL + level (skippable)
  void showBootStats(uint32_t highScore, uint8_t highLevel, AbortFn abortFn = nullptr);

  void render(const TetrisGame& g, uint32_t nowMs);

private:
  uint16_t XY(uint8_t x, uint8_t y) const;

  // int16 so we can draw off-screen (for scrolling)
  void setPixel(int16_t x, int16_t y, uint32_t c);

  uint32_t rgb(uint32_t rrggbb) const;

  uint8_t themeIndex(uint8_t level) const;
  uint32_t pieceColor(uint8_t level, uint8_t pieceId) const;
  uint32_t scaleColor(uint32_t c, uint8_t alpha) const;

  void drawChar(int16_t x, int16_t y, char c, uint32_t color);
  void drawTextCentered(String text, int16_t y, uint32_t color);
  void drawText(int16_t x, int16_t y, const String& text, uint32_t color);

  // Two-line screen: title on top, value on bottom (scrolls if needed). Returns true if aborted.
  bool showTwoLineTitleValue(const String& title, const String& value,
                             uint32_t titleColor, uint32_t valueColor,
                             uint32_t durationMs,
                             AbortFn abortFn);

  uint32_t arcadeBorderColor(const TetrisGame& g, uint8_t x, uint8_t y, uint32_t nowMs) const;
  uint32_t solidLevelBorderColor(const TetrisGame& g, uint8_t x, uint8_t y, uint32_t nowMs) const;

private:
  Adafruit_NeoPixel strip_;
};
