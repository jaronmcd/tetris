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
  
  // Shows "LVL" -> Val -> "SCR" -> Val in a loop
  void showGameOver(uint32_t score, uint8_t level);

  void render(const TetrisGame& g, uint32_t nowMs);

private:
  uint16_t XY(uint8_t x, uint8_t y) const;
  void setPixel(uint8_t x, uint8_t y, uint32_t c);
  uint32_t rgb(uint32_t rrggbb) const;

  // Visual helpers
  uint8_t themeIndex(uint8_t level) const;
  uint32_t pieceColor(uint8_t level, uint8_t pieceId) const;
  uint32_t scaleColor(uint32_t c, uint8_t alpha) const;

  // Text helpers
  void drawChar(int16_t x, int16_t y, char c, uint32_t color);
  void drawTextCentered(String text, int16_t y, uint32_t color);

  // Border logic
  uint32_t arcadeBorderColor(const TetrisGame& g, uint8_t x, uint8_t y, uint32_t nowMs) const;

private:
  Adafruit_NeoPixel strip_;
};