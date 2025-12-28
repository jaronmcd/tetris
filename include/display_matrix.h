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
  void levelUpFlash();               // called when game reports level-up
  void render(const TetrisGame& g, uint32_t nowMs);

private:
  uint16_t XY(uint8_t x, uint8_t y) const;
  void setPixel(uint8_t x, uint8_t y, uint32_t c);

  uint32_t rgb(uint32_t rrggbb) const;

  // Themes / level visuals
  uint8_t themeIndex(uint8_t level) const;
  uint32_t pieceColor(uint8_t level, uint8_t pieceId) const;

  // Color helpers
  uint32_t scaleColor(uint32_t c, uint8_t alpha) const; // alpha: 0..255

  // Arcade border
  uint8_t borderTimeShiftForLevel(uint8_t level) const;
  uint8_t tri8(uint8_t x) const;
  void wheel(uint8_t pos, uint8_t& r, uint8_t& g, uint8_t& b) const;
  uint32_t arcadeBorderColor(uint8_t level, uint8_t x, uint8_t y, uint32_t nowMs) const;

private:
  Adafruit_NeoPixel strip_;
};
