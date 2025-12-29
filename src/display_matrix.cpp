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
  // Letters A-Z
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

static constexpr uint8_t NUM_THEMES = 5;
static const uint32_t THEMES[NUM_THEMES][8] = {
  {0, 0x00FFFF, 0xFFFF00, 0xAA00FF, 0x00FF00, 0xFF0000, 0x0000FF, 0xFF7F00},
  {0, 0x00F0FF, 0xFFD000, 0xFF00FF, 0x00FF66, 0xFF0066, 0x3B6CFF, 0xFF4D00},
  {0, 0xA0FFFF, 0xE8F7FF, 0x80A0FF, 0x60FFCC, 0xFF80A0, 0x60A0FF, 0xA0D8FF},
  {0, 0xFFAA00, 0xFFDD00, 0xFF3300, 0xFF6600, 0xCC0000, 0xFF8800, 0xFF4400},
  {0, 0x00E5FF, 0xFFF400, 0xFF3DF2, 0x00FF9A, 0xFF2D55, 0x6C63FF, 0xFF7A00},
};

// State Vars
static uint8_t  g_prevLocked = 255;
static uint16_t g_hueOld = 0;
static uint16_t g_hueNew = 0;
static uint32_t g_wipeStartMs = 0;
static bool     g_firstRun = true;

// Fade State
static bool     g_isFading = false;
static uint32_t g_fadeStartMs = 0;
static uint8_t  g_fadeFromLevel = 0;
static uint8_t  g_fadeToLevel = 0;
static uint32_t g_fadeDuration = 2000; 

static uint16_t rgbToHue(uint32_t c) {
  uint8_t r = (uint8_t)(c >> 16); uint8_t g = (uint8_t)(c >> 8); uint8_t b = (uint8_t)c;
  uint8_t minVal = min(r, min(g, b)); uint8_t maxVal = max(r, max(g, b));
  uint8_t delta = maxVal - minVal;
  if (delta == 0) return 0; 
  int32_t hue;
  if (r == maxVal) hue = (int32_t)65536 * (g - b) / (6 * delta);
  else if (g == maxVal) hue = (int32_t)65536 * (2 * delta + (b - r)) / (6 * delta);
  else hue = (int32_t)65536 * (4 * delta + (r - g)) / (6 * delta);
  if (hue < 0) hue += 65536;
  return (uint16_t)hue;
}

static uint32_t lerpColorRGB(uint32_t c1, uint32_t c2, uint8_t step) {
  if (step == 0) return c1; if (step == 255) return c2;
  uint8_t r1 = (uint8_t)(c1 >> 16); uint8_t g1 = (uint8_t)(c1 >> 8); uint8_t b1 = (uint8_t)c1;
  uint8_t r2 = (uint8_t)(c2 >> 16); uint8_t g2 = (uint8_t)(c2 >> 8); uint8_t b2 = (uint8_t)c2;
  return ((uint32_t)(r1 + (((r2-r1)*step)>>8)) << 16) | ((uint32_t)(g1 + (((g2-g1)*step)>>8)) << 8) | (b1 + (((b2-b1)*step)>>8));
}

MatrixDisplay::MatrixDisplay() : strip_(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800) {}

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

void MatrixDisplay::levelUpFlash(uint8_t nextLevel) {
  g_isFading = true;
  g_fadeStartMs = millis();
  g_fadeToLevel = nextLevel;
  g_fadeFromLevel = (nextLevel > 1) ? (nextLevel - 1) : 0;
}

// --- TEXT FUNCTIONS ---
void MatrixDisplay::drawChar(int16_t x, int16_t y, char c, uint32_t color) {
  int idx = -1;
  if (c >= '0' && c <= '9') idx = c - '0';
  else if (c >= 'A' && c <= 'Z') idx = 12 + (c - 'A');
  else if (c >= 'a' && c <= 'z') idx = 12 + (c - 'a');
  else if (c == ':') idx = 10;
  
  if (idx == -1) return; 

  if (idx == 10) { 
      setPixel(x+1, y+1, color);
      setPixel(x+1, y+3, color);
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
  int totalWidth = text.length() * 4 - 1; // 3px char + 1px space
  int startX = (MATRIX_W - totalWidth) / 2;
  if (startX < 0) startX = 0; 

  for (int i = 0; i < (int)text.length(); i++) {
    drawChar(startX + (i * 4), y, text[i], color);
  }
}

// --- PAGED GAME OVER ---
void MatrixDisplay::showGameOver(uint32_t score, uint8_t level) {
  // Colors
  uint32_t cLevelLabel = strip_.Color(0, 0, 255);   // Blue "LVL"
  uint32_t cLevelVal   = strip_.Color(255, 255, 255); // White #
  uint32_t cScoreLabel = strip_.Color(0, 255, 0);   // Green "SCR"
  uint32_t cScoreVal   = strip_.Color(255, 255, 255); // White #

  // Cycle 3 times
  for (int i = 0; i < 3; i++) {
    
    // 1. "LVL"
    strip_.clear();
    drawTextCentered("LVL", 6, cLevelLabel);
    strip_.show();
    delay(1500);

    // 2. Level Number
    strip_.clear();
    drawTextCentered(String(level), 6, cLevelVal);
    strip_.show();
    delay(1500);

    // 3. "SCR"
    strip_.clear();
    drawTextCentered("SCR", 6, cScoreLabel);
    strip_.show();
    delay(1500);

    // 4. Score Number
    strip_.clear();
    drawTextCentered(String(score), 6, cScoreVal);
    strip_.show();
    delay(1500);
  }
  
  strip_.clear();
  strip_.show();
  delay(500);
}

// --- STANDARD RENDER ---
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
  uint8_t r = (rrggbb >> 16) & 0xFF; uint8_t g = (rrggbb >> 8) & 0xFF; uint8_t b = (rrggbb) & 0xFF;
  return strip_.Color(r, g, b);
}

uint8_t MatrixDisplay::themeIndex(uint8_t level) const { return (uint8_t)((level - 1) % NUM_THEMES); }

uint32_t MatrixDisplay::pieceColor(uint8_t level, uint8_t pieceId) const {
  if (pieceId > 7) pieceId = 0;
  return rgb(THEMES[themeIndex(level)][pieceId]);
}

uint32_t MatrixDisplay::scaleColor(uint32_t c, uint8_t alpha) const {
  uint8_t r = (uint8_t)((c >> 16) & 0xFF); uint8_t g = (uint8_t)((c >> 8) & 0xFF); uint8_t b = (uint8_t)(c & 0xFF);
  r = (uint8_t)(((uint16_t)r * alpha) >> 8); g = (uint8_t)(((uint16_t)g * alpha) >> 8); b = (uint8_t)(((uint16_t)b * alpha) >> 8);
  return strip_.Color(r, g, b);
}

uint32_t MatrixDisplay::arcadeBorderColor(const TetrisGame& g, uint8_t x, uint8_t y, uint32_t nowMs) const {
  uint16_t finalHue;
  if (g_isFading) {
    uint32_t elapsed = nowMs - g_fadeStartMs;
    if (elapsed > g_fadeDuration) elapsed = g_fadeDuration; 
    uint32_t totalSteps = 14; 
    uint32_t currentStep = (elapsed * totalSteps) / g_fadeDuration;
    if (currentStep >= totalSteps) currentStep = totalSteps - 1;
    uint8_t cIdx = currentStep % 7; uint8_t nextCIdx = (cIdx + 1) % 7;
    uint8_t tIdx = themeIndex(g_fadeToLevel);
    uint32_t c1 = THEMES[tIdx][cIdx + 1]; uint32_t c2 = THEMES[tIdx][nextCIdx + 1];
    uint32_t stepDur = g_fadeDuration / totalSteps;
    uint32_t subTime = elapsed - ((currentStep * g_fadeDuration) / totalSteps);
    uint8_t splitY = (subTime * MATRIX_H) / stepDur;
    uint32_t targetC = (y < splitY) ? c2 : c1;
    finalHue = rgbToHue(targetC);
  } else {
    uint32_t duration = 600; 
    uint32_t dt = (nowMs >= g_wipeStartMs) ? (nowMs - g_wipeStartMs) : duration;
    uint8_t splitY = (dt >= duration) ? MATRIX_H : (uint8_t)((dt * MATRIX_H) / duration);
    uint16_t baseHue = (y < splitY) ? g_hueNew : g_hueOld;
    uint16_t rawScroll = (uint16_t)((x + y) * 120 - (nowMs * 30));
    finalHue = baseHue + (rawScroll >> 5);
  }
  bool isMeshGap = ((x ^ y) & 1);
  return strip_.ColorHSV(finalHue, 200, isMeshGap ? 40 : 140);
}

void MatrixDisplay::render(const TetrisGame& g, uint32_t nowMs) {
  strip_.clear();
  const uint8_t lvl = g.level();
  uint8_t tIdx = themeIndex(lvl);
  uint8_t locked = g.lastLockedPieceType();
  if (g_firstRun) {
    g_prevLocked = locked;
    g_hueNew = rgbToHue(THEMES[tIdx][locked + 1]) + 32768; 
    g_hueOld = g_hueNew;
    g_firstRun = false;
  }
  else if (locked != g_prevLocked) {
    g_hueOld = g_hueNew; 
    g_hueNew = rgbToHue(THEMES[tIdx][locked + 1]) + 32768;
    g_wipeStartMs = nowMs;
    g_prevLocked = locked;
  }

  uint8_t fadeStep = 255; 
  if (g_isFading) {
    uint32_t elapsed = nowMs - g_fadeStartMs;
    if (elapsed >= g_fadeDuration) g_isFading = false;
    else fadeStep = (uint8_t)((elapsed * 255) / g_fadeDuration);
  }

  for (uint8_t y = 0; y < MATRIX_H; y++) {
    for (uint8_t x = 0; x < MATRIX_W; x++) {
      bool inBoardX = (x >= BOARD_OFFSET_X) && (x < (BOARD_OFFSET_X + BOARD_W));
      bool inBoardY = (y >= BOARD_OFFSET_Y) && (y < (BOARD_OFFSET_Y + BOARD_H));
      if (!inBoardX || !inBoardY) {
        setPixel(x, y, arcadeBorderColor(g, x, y, nowMs));
      }
    }
  }

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
      if (g_isFading) c = lerpColorRGB(pieceColor(g_fadeFromLevel, id), pieceColor(g_fadeToLevel, id), fadeStep);
      else c = pieceColor(lvl, id);

      if (clearing && g.isClearingRow(by)) {
        uint8_t lines = g.clearingLineCount();
        if (lines >= 4) c = strip_.ColorHSV((bx * 4000) + (nowMs * 60), 255, alpha); // Rainbow
        else if (lines == 3) { // 3 Flash
           if (elapsed < 80 || (elapsed >= 160 && elapsed < 240) || (elapsed >= 320 && elapsed < 400)) c = strip_.Color(255,255,255);
           else c = scaleColor(c, alpha);
        } else if (lines == 2) { // 2 Flash
           if (elapsed < 80 || (elapsed >= 160 && elapsed < 240)) c = strip_.Color(255,255,255);
           else c = scaleColor(c, alpha);
        } else { // 1 Flash
           if (elapsed < 60) c = strip_.Color(255, 255, 255);
           else c = scaleColor(c, alpha);
        }
      }
      setPixel(BOARD_OFFSET_X + bx, BOARD_OFFSET_Y + by, c);
    }
  }

  if (!g.isGameOver() && !clearing) {
    TetrisGame::Cell cells[4]; uint8_t n = 0;
    g.getCurrentPieceBlocks(cells, n);
    uint8_t id = g.currentPieceId();
    uint32_t c;
    if (g_isFading) c = lerpColorRGB(pieceColor(g_fadeFromLevel, id), pieceColor(g_fadeToLevel, id), fadeStep);
    else c = pieceColor(lvl, id);
    for (uint8_t i = 0; i < n; i++) setPixel(BOARD_OFFSET_X + cells[i].x, BOARD_OFFSET_Y + cells[i].y, c);
  } 

  if (g.isGameOver()) {
    strip_.fill(strip_.Color(20, 0, 0)); 
  }

  strip_.show();
}