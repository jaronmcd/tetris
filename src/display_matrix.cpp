#include "display_matrix.h"
#include <Arduino.h>

static constexpr uint8_t NUM_THEMES = 5;

// THEMES[theme][pieceId 0..7], rrggbb
static const uint32_t THEMES[NUM_THEMES][8] = {
  {0, 0x00FFFF, 0xFFFF00, 0xAA00FF, 0x00FF00, 0xFF0000, 0x0000FF, 0xFF7F00}, // Classic
  {0, 0x00F0FF, 0xFFD000, 0xFF00FF, 0x00FF66, 0xFF0066, 0x3B6CFF, 0xFF4D00}, // Neon
  {0, 0xA0FFFF, 0xE8F7FF, 0x80A0FF, 0x60FFCC, 0xFF80A0, 0x60A0FF, 0xA0D8FF}, // Ice
  {0, 0xFFAA00, 0xFFDD00, 0xFF3300, 0xFF6600, 0xCC0000, 0xFF8800, 0xFF4400}, // Lava
  {0, 0x00E5FF, 0xFFF400, 0xFF3DF2, 0x00FF9A, 0xFF2D55, 0x6C63FF, 0xFF7A00}, // Synthwave
};

MatrixDisplay::MatrixDisplay()
: strip_(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800) {}

void MatrixDisplay::begin() {
  strip_.begin();
  strip_.setBrightness(BRIGHTNESS);
  strip_.clear();
  strip_.show();
}

void MatrixDisplay::bootFlash() {
  strip_.fill(strip_.Color(20, 0, 0)); strip_.show(); delay(120);
  strip_.fill(strip_.Color(0, 20, 0)); strip_.show(); delay(120);
  strip_.fill(strip_.Color(0, 0, 20)); strip_.show(); delay(120);
  strip_.clear(); strip_.show();
}

void MatrixDisplay::levelUpFlash() {
  // Quick arcade “pop” (brief, slightly brighter)
  uint8_t oldB = BRIGHTNESS;

  strip_.setBrightness(40);
  strip_.fill(strip_.Color(80, 80, 80));
  strip_.show();
  delay(55);

  strip_.clear();
  strip_.show();
  delay(35);

  strip_.fill(strip_.Color(80, 80, 80));
  strip_.show();
  delay(55);

  strip_.setBrightness(oldB);
  strip_.clear();
  strip_.show();
}

uint16_t MatrixDisplay::XY(uint8_t x, uint8_t y) const {
  if (x >= MATRIX_W || y >= MATRIX_H) return 0;
  uint8_t yy = MATRIX_BOTTOM_UP ? (MATRIX_H - 1 - y) : y;

  if (!SERPENTINE) return (uint16_t)yy * MATRIX_W + x;
  if ((yy & 1) == 0) return (uint16_t)yy * MATRIX_W + x;
  return (uint16_t)yy * MATRIX_W + (MATRIX_W - 1 - x);
}

void MatrixDisplay::setPixel(uint8_t x, uint8_t y, uint32_t c) {
  strip_.setPixelColor(XY(x, y), c);
}

uint32_t MatrixDisplay::rgb(uint32_t rrggbb) const {
  uint8_t r = (rrggbb >> 16) & 0xFF;
  uint8_t g = (rrggbb >> 8) & 0xFF;
  uint8_t b = (rrggbb) & 0xFF;
  return strip_.Color(r, g, b);
}

uint8_t MatrixDisplay::themeIndex(uint8_t level) const {
  return (uint8_t)((level - 1) % NUM_THEMES);
}

uint32_t MatrixDisplay::pieceColor(uint8_t level, uint8_t pieceId) const {
  return rgb(THEMES[themeIndex(level)][pieceId]);
}

uint8_t MatrixDisplay::borderTimeShiftForLevel(uint8_t level) const {
  if (level <= 1) return 6;
  if (level <= 3) return 5;
  if (level <= 6) return 4;
  if (level <= 10) return 3;
  if (level <= 15) return 2;
  return 1;
}

uint8_t MatrixDisplay::tri8(uint8_t x) const {
  return (x & 0x80) ? (uint8_t)(255 - ((x & 0x7F) << 1)) : (uint8_t)((x & 0x7F) << 1);
}

void MatrixDisplay::wheel(uint8_t pos, uint8_t& r, uint8_t& g, uint8_t& b) const {
  pos = 255 - pos;
  if (pos < 85) {
    r = 255 - pos * 3; g = 0;          b = pos * 3;
  } else if (pos < 170) {
    pos -= 85;
    r = 0;          g = pos * 3;       b = 255 - pos * 3;
  } else {
    pos -= 170;
    r = pos * 3;    g = 255 - pos * 3; b = 0;
  }
}

uint32_t MatrixDisplay::arcadeBorderColor(uint8_t level, uint8_t x, uint8_t y, uint32_t nowMs) const {
  int dx = 0, dy = 0;

  if (x < BOARD_OFFSET_X) dx = BOARD_OFFSET_X - x;
  else if (x >= (BOARD_OFFSET_X + BOARD_W)) dx = x - (BOARD_OFFSET_X + BOARD_W - 1);

  if (y < BOARD_OFFSET_Y) dy = BOARD_OFFSET_Y - y;
  else if (y >= (BOARD_OFFSET_Y + BOARD_H)) dy = y - (BOARD_OFFSET_Y + BOARD_H - 1);

  int dist = max(dx, dy);
  if (dist <= 0) return 0;

  const int maxDist =
      max(max(BOARD_OFFSET_X, (MATRIX_W - (BOARD_OFFSET_X + BOARD_W))),
          max(BOARD_OFFSET_Y, (MATRIX_H - (BOARD_OFFSET_Y + BOARD_H))));

  uint8_t tShift = borderTimeShiftForLevel(level);
  uint8_t theme = themeIndex(level);
  uint8_t themeOffset = (uint8_t)(theme * 48);

  uint8_t hue = (uint8_t)(x * 17 + y * 23 + (nowMs >> tShift) + themeOffset);

  uint8_t r, g, b;
  wheel(hue, r, g, b);

  uint8_t fade = 70;
  if (maxDist > 1) {
    fade = (uint8_t)(70 + (185 * (maxDist - (dist - 1))) / maxDist);
  } else {
    fade = 255;
  }

  uint8_t pulse = (uint8_t)(190 + (tri8((uint8_t)(nowMs >> 5)) >> 2));

  uint16_t scale16 = (uint16_t)fade * (uint16_t)pulse;
  uint8_t scale = (uint8_t)(scale16 >> 8);

  r = (uint8_t)(((uint16_t)r * scale) >> 8);
  g = (uint8_t)(((uint16_t)g * scale) >> 8);
  b = (uint8_t)(((uint16_t)b * scale) >> 8);

  return strip_.Color(r, g, b);
}

void MatrixDisplay::render(const TetrisGame& g, uint32_t nowMs) {
  strip_.clear();

  const uint8_t lvl = g.level();

  // Border (arcade)
  for (uint8_t y = 0; y < MATRIX_H; y++) {
    for (uint8_t x = 0; x < MATRIX_W; x++) {
      bool inBoardX = (x >= BOARD_OFFSET_X) && (x < (BOARD_OFFSET_X + BOARD_W));
      bool inBoardY = (y >= BOARD_OFFSET_Y) && (y < (BOARD_OFFSET_Y + BOARD_H));
      if (!inBoardX || !inBoardY) {
        setPixel(x, y, arcadeBorderColor(lvl, x, y, nowMs));
      }
    }
  }

  // Locked blocks
  auto b = g.board();
  for (uint8_t by = 0; by < BOARD_H; by++) {
    for (uint8_t bx = 0; bx < BOARD_W; bx++) {
      uint8_t id = b[by][bx];
      if (id) {
        setPixel(BOARD_OFFSET_X + bx, BOARD_OFFSET_Y + by, pieceColor(lvl, id));
      }
    }
  }

  // Current piece
  if (!g.isGameOver()) {
    TetrisGame::Cell cells[4];
    uint8_t n = 0;
    g.getCurrentPieceBlocks(cells, n);
    uint8_t id = g.currentPieceId();
    uint32_t c = pieceColor(lvl, id);
    for (uint8_t i = 0; i < n; i++) {
      setPixel(BOARD_OFFSET_X + (uint8_t)cells[i].x, BOARD_OFFSET_Y + (uint8_t)cells[i].y, c);
    }
  } else {
    bool on = ((nowMs / 350) & 1) == 0;
    uint32_t c = on ? strip_.Color(40, 0, 0) : strip_.Color(0, 0, 0);
    for (uint8_t y = 0; y < MATRIX_H; y++)
      for (uint8_t x = 0; x < MATRIX_W; x++)
        setPixel(x, y, c);
  }

  strip_.show();
}
