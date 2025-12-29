#pragma once
#include <stdint.h>
#include <Adafruit_NeoPixel.h>

#include "config.h"
#include "tetris.h"

class MatrixDisplay {
public:
  MatrixDisplay();

  void begin();
  void bootFlash();
  void levelUpFlash(uint8_t nextLevel);

  // Normal Game Over (Score + Level)
  void showGameOver(uint32_t score, uint8_t level);

  // NEW: Celebration for beating the record
  void showNewHighScore(uint32_t score);

  // Boot: "HI" -> Score -> "LVL" -> Level
  void showBootStats(uint32_t highScore, uint8_t highLevel);

  void render(const TetrisGame& g, uint32_t nowMs);

private:
  uint16_t XY(uint8_t x, uint8_t y) const;

  // NOTE: int16 so we can draw off-screen (for scrolling)
  void setPixel(int16_t x, int16_t y, uint32_t c);

  uint32_t rgb(uint32_t rrggbb) const;

  uint8_t themeIndex(uint8_t level) const;
  uint32_t pieceColor(uint8_t level, uint8_t pieceId) const;
  uint32_t scaleColor(uint32_t c, uint8_t alpha) const;

  void drawChar(int16_t x, int16_t y, char c, uint32_t color);
  void drawTextCentered(String text, int16_t y, uint32_t color);

  // NEW: draw a string at exact x (can be negative)
  void drawText(int16_t x, int16_t y, const String& text, uint32_t color);

  // NEW: two-line screen: title top, value bottom (scrolls if too wide)
  void showTwoLineTitleValue(const String& title, const String& value,
                             uint32_t titleColor, uint32_t valueColor,
                             uint32_t durationMs);

  uint32_t arcadeBorderColor(const TetrisGame& g, uint8_t x, uint8_t y, uint32_t nowMs) const;

private:
  Adafruit_NeoPixel strip_;
};
