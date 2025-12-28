#include "display_matrix.h"
#include <Arduino.h>

static constexpr uint8_t NUM_THEMES = 5;

// --- GAME PIECE COLORS ---
static const uint32_t THEMES[NUM_THEMES][8] = {
  {0, 0x00FFFF, 0xFFFF00, 0xAA00FF, 0x00FF00, 0xFF0000, 0x0000FF, 0xFF7F00}, // Classic
  {0, 0x00F0FF, 0xFFD000, 0xFF00FF, 0x00FF66, 0xFF0066, 0x3B6CFF, 0xFF4D00}, // Neon
  {0, 0xA0FFFF, 0xE8F7FF, 0x80A0FF, 0x60FFCC, 0xFF80A0, 0x60A0FF, 0xA0D8FF}, // Ice
  {0, 0xFFAA00, 0xFFDD00, 0xFF3300, 0xFF6600, 0xCC0000, 0xFF8800, 0xFF4400}, // Lava
  {0, 0x00E5FF, 0xFFF400, 0xFF3DF2, 0x00FF9A, 0xFF2D55, 0x6C63FF, 0xFF7A00}, // Synthwave
};

// --- STATE VARIABLES ---
static uint8_t  g_prevLocked = 255;
static uint16_t g_hueOld = 0;
static uint16_t g_hueNew = 0;
static uint32_t g_wipeStartMs = 0;
static bool     g_firstRun = true;

// --- FADE STATE ---
static bool     g_isFading = false;
static uint32_t g_fadeStartMs = 0;
static uint8_t  g_fadeFromLevel = 0;
static uint8_t  g_fadeToLevel = 0;

// Helper: Convert RGB to Hue (0-65535)
static uint16_t rgbToHue(uint32_t c) {
  uint8_t r = (uint8_t)(c >> 16);
  uint8_t g = (uint8_t)(c >> 8);
  uint8_t b = (uint8_t)c;
  uint8_t minVal = min(r, min(g, b));
  uint8_t maxVal = max(r, max(g, b));
  uint8_t delta = maxVal - minVal;
  if (delta == 0) return 0; 
  int32_t hue;
  if (r == maxVal) {
    hue = (int32_t)65536 * (g - b) / (6 * delta);
    if (hue < 0) hue += 65536;
  } else if (g == maxVal) {
    hue = (int32_t)65536 * (2 * delta + (b - r)) / (6 * delta);
  } else {
    hue = (int32_t)65536 * (4 * delta + (r - g)) / (6 * delta);
  }
  return (uint16_t)hue;
}

// Helper: Linear Interpolate between two RGB colors
static uint32_t lerpColorRGB(uint32_t c1, uint32_t c2, uint8_t step) {
  // step is 0..255
  if (step == 0) return c1;
  if (step == 255) return c2;

  uint8_t r1 = (uint8_t)(c1 >> 16);
  uint8_t g1 = (uint8_t)(c1 >> 8);
  uint8_t b1 = (uint8_t)c1;

  uint8_t r2 = (uint8_t)(c2 >> 16);
  uint8_t g2 = (uint8_t)(c2 >> 8);
  uint8_t b2 = (uint8_t)c2;

  uint8_t r = r1 + (((r2 - r1) * step) >> 8);
  uint8_t g = g1 + (((g2 - g1) * step) >> 8);
  uint8_t b = b1 + (((b2 - b1) * step) >> 8);

  return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

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

// --- LEVEL UP: TRIGGER CROSS-FADE (NON-BLOCKING) ---
void MatrixDisplay::levelUpFlash(uint8_t nextLevel) {
  // We simply set the state here. The render loop handles the rest.
  g_isFading = true;
  g_fadeStartMs = millis();
  g_fadeToLevel = nextLevel;
  g_fadeFromLevel = (nextLevel > 1) ? (nextLevel - 1) : 0;
  
  // Optional: Tiny brightness pop that doesn't block gameplay
  // We can't delay here, so we skip the flash or handle it elsewhere.
  // For smooth gameplay, we do nothing blocking.
}

uint16_t MatrixDisplay::XY(uint8_t x, uint8_t y) const {
  if (x >= MATRIX_W || y >= MATRIX_H) return 0;
  uint8_t xx = (uint8_t)(MATRIX_W - 1 - x);
  uint8_t yy = MATRIX_BOTTOM_UP ? (MATRIX_H - 1 - y) : y;
  if (!SERPENTINE) return (uint16_t)yy * MATRIX_W + xx;
  if ((yy & 1) == 0) return (uint16_t)yy * MATRIX_W + xx;
  return (uint16_t)yy * MATRIX_W + (MATRIX_W - 1 - xx);
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

// Modified pieceColor to optionally blend themes
uint32_t MatrixDisplay::pieceColor(uint8_t level, uint8_t pieceId) const {
  if (pieceId > 7) pieceId = 0;
  return rgb(THEMES[themeIndex(level)][pieceId]);
}

uint32_t MatrixDisplay::scaleColor(uint32_t c, uint8_t alpha) const {
  uint8_t r = (uint8_t)((c >> 16) & 0xFF);
  uint8_t g = (uint8_t)((c >> 8) & 0xFF);
  uint8_t b = (uint8_t)(c & 0xFF);
  r = (uint8_t)(((uint16_t)r * alpha) >> 8);
  g = (uint8_t)(((uint16_t)g * alpha) >> 8);
  b = (uint8_t)(((uint16_t)b * alpha) >> 8);
  return strip_.Color(r, g, b);
}

uint8_t MatrixDisplay::borderTimeShiftForLevel(uint8_t level) const {
  if (level <= 1) return 4; 
  if (level <= 3) return 3;
  if (level <= 6) return 2;
  if (level <= 10) return 1;
  return 0; 
}

uint8_t MatrixDisplay::tri8(uint8_t x) const { return 0; }
void MatrixDisplay::wheel(uint8_t, uint8_t&, uint8_t&, uint8_t&) const {}

// -------------------------------------------------------------------------
// BORDER COLOR
// -------------------------------------------------------------------------
uint32_t MatrixDisplay::arcadeBorderColor(const TetrisGame& g, uint8_t x, uint8_t y, uint32_t nowMs) const {
  uint32_t duration = 600; 
  uint32_t dt = (nowMs >= g_wipeStartMs) ? (nowMs - g_wipeStartMs) : duration;
  uint8_t splitY = (dt >= duration) ? MATRIX_H : (uint8_t)((dt * MATRIX_H) / duration);

  uint16_t baseHue = (y < splitY) ? g_hueNew : g_hueOld;

  uint16_t rawScroll = (uint16_t)((x + y) * 120 - (nowMs * 30));
  uint16_t rippleOffset = (rawScroll >> 5); 

  uint16_t finalHue = baseHue + rippleOffset;

  bool isMeshGap = ((x ^ y) & 1);
  uint8_t val = isMeshGap ? 40 : 140; 
  uint8_t sat = 200;

  return strip_.ColorHSV(finalHue, sat, val);
}

void MatrixDisplay::render(const TetrisGame& g, uint32_t nowMs) {
  strip_.clear();

  const uint8_t lvl = g.level();
  uint8_t tIdx = themeIndex(lvl);

  // --- HANDLE BORDER LOGIC ---
  uint8_t locked = g.lastLockedPieceType();
  if (g_firstRun) {
    g_prevLocked = locked;
    uint32_t c = THEMES[tIdx][locked + 1];
    g_hueNew = rgbToHue(c) + 32768; 
    g_hueOld = g_hueNew;
    g_firstRun = false;
  }
  else if (locked != g_prevLocked) {
    g_hueOld = g_hueNew; 
    uint32_t c = THEMES[tIdx][locked + 1];
    g_hueNew = rgbToHue(c) + 32768;
    g_wipeStartMs = nowMs;
    g_prevLocked = locked;
  }

  // --- HANDLE BLOCK FADE LOGIC ---
  uint8_t fadeStep = 255; // Default: fully new color
  if (g_isFading) {
    uint32_t elapsed = nowMs - g_fadeStartMs;
    if (elapsed >= 2000) { // 2.0 Seconds fade
      g_isFading = false;
    } else {
      // Scale 0..2000ms to 0..255
      fadeStep = (uint8_t)((elapsed * 255) / 2000);
    }
  }

  // Draw Border
  for (uint8_t y = 0; y < MATRIX_H; y++) {
    for (uint8_t x = 0; x < MATRIX_W; x++) {
      bool inBoardX = (x >= BOARD_OFFSET_X) && (x < (BOARD_OFFSET_X + BOARD_W));
      bool inBoardY = (y >= BOARD_OFFSET_Y) && (y < (BOARD_OFFSET_Y + BOARD_H));
      if (!inBoardX || !inBoardY) {
        setPixel(x, y, arcadeBorderColor(g, x, y, nowMs));
      }
    }
  }

  // Draw Board
  auto b = g.board();
  const bool clearing = g.isClearingLines();
  const uint32_t elapsed = clearing ? g.clearingElapsedMs(nowMs) : 0;
  const uint8_t alphaLin = g.clearingAlpha(nowMs);
  const uint8_t alpha = (uint8_t)(((uint16_t)alphaLin * (uint16_t)alphaLin) >> 8);

  for (uint8_t by = 0; by < BOARD_H; by++) {
    for (uint8_t bx = 0; bx < BOARD_W; bx++) {
      uint8_t id = b[by][bx];
      if (!id) continue;

      uint32_t c;

      if (g_isFading) {
        // BLEND COLORS: Old Theme -> New Theme
        uint32_t cOld = pieceColor(g_fadeFromLevel, id);
        uint32_t cNew = pieceColor(g_fadeToLevel, id);
        uint32_t blended = lerpColorRGB(cOld, cNew, fadeStep);
        c = blended;
      } else {
        // Standard single color
        c = pieceColor(lvl, id);
      }

      if (clearing && g.isClearingRow(by)) {
        if (elapsed < 60) c = strip_.Color(255, 255, 255);
        else c = scaleColor(c, alpha);
      }
      setPixel(BOARD_OFFSET_X + bx, BOARD_OFFSET_Y + by, c);
    }
  }

  // Draw Current Piece
  if (!g.isGameOver() && !clearing) {
    TetrisGame::Cell cells[4];
    uint8_t n = 0;
    g.getCurrentPieceBlocks(cells, n);
    uint8_t id = g.currentPieceId();
    
    uint32_t c;
    if (g_isFading) {
        uint32_t cOld = pieceColor(g_fadeFromLevel, id);
        uint32_t cNew = pieceColor(g_fadeToLevel, id);
        c = lerpColorRGB(cOld, cNew, fadeStep);
    } else {
        c = pieceColor(lvl, id);
    }

    for (uint8_t i = 0; i < n; i++) {
      setPixel(BOARD_OFFSET_X + (uint8_t)cells[i].x, BOARD_OFFSET_Y + (uint8_t)cells[i].y, c);
    }
  } 
  
  // Game Over
  if (g.isGameOver()) {
    bool on = ((nowMs / 350) & 1) == 0;
    uint32_t c = on ? strip_.Color(40, 0, 0) : strip_.Color(0, 0, 0);
    for (uint8_t y = 0; y < MATRIX_H; y++) {
      for (uint8_t x = 0; x < MATRIX_W; x++) {
        setPixel(x, y, c);
      }
    }
  }

  strip_.show();
}