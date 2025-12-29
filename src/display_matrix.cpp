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

static constexpr uint8_t NUM_THEMES = 5;
static const uint32_t THEMES[NUM_THEMES][8] = {
  {0, 0x00FFFF, 0xFFFF00, 0xAA00FF, 0x00FF00, 0xFF0000, 0x0000FF, 0xFF7F00},
  {0, 0x00F0FF, 0xFFD000, 0xFF00FF, 0x00FF66, 0xFF0066, 0x3B6CFF, 0xFF4D00},
  {0, 0xA0FFFF, 0xE8F7FF, 0x80A0FF, 0x60FFCC, 0xFF80A0, 0x60A0FF, 0xA0D8FF},
  {0, 0xFFAA00, 0xFFDD00, 0xFF3300, 0xFF6600, 0xCC0000, 0xFF8800, 0xFF4400},
  {0, 0x00E5FF, 0xFFF400, 0xFF3DF2, 0x00FF9A, 0xFF2D55, 0x6C63FF, 0xFF7A00},
};

// Border / theme animation state
static uint8_t  g_prevLocked = 255;
static uint16_t g_hueOld = 0;
static uint16_t g_hueNew = 0;
static uint32_t g_wipeStartMs = 0;
static bool     g_firstRun = true;

// Level-up fade animation state
static bool     g_isFading = false;
static uint32_t g_fadeStartMs = 0;
static uint8_t  g_fadeFromLevel = 0;
static uint8_t  g_fadeToLevel = 0;
static uint32_t g_fadeDuration = 2000;

// Level-up waterfall animation state (border)
static bool     g_waterfallActive = false;
static uint32_t g_waterfallStartMs = 0;
static uint32_t g_waterfallDuration = 950;   // tweak for faster/slower waterfall
static uint8_t  g_waterfallFromLevel = 1;
static uint8_t  g_waterfallToLevel = 1;

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
  if (step == 0) return c1;
  if (step == 255) return c2;
  uint8_t r1 = (uint8_t)(c1 >> 16); uint8_t g1 = (uint8_t)(c1 >> 8); uint8_t b1 = (uint8_t)c1;
  uint8_t r2 = (uint8_t)(c2 >> 16); uint8_t g2 = (uint8_t)(c2 >> 8); uint8_t b2 = (uint8_t)c2;
  return ((uint32_t)(r1 + (((r2 - r1) * step) >> 8)) << 16) |
         ((uint32_t)(g1 + (((g2 - g1) * step) >> 8)) << 8) |
         (b1 + (((b2 - b1) * step) >> 8));
}

static uint16_t hueDistance(uint16_t a, uint16_t b) {
  uint16_t d = (a > b) ? (a - b) : (b - a);
  if (d > 32768) d = (uint16_t)(65536 - d);
  return d;
}

static inline uint8_t themeIndexStatic(uint8_t level) {
  if (level < 1) level = 1;
  return (uint8_t)((level - 1) % NUM_THEMES);
}

// Pick a border hue for each level that:
// 1) Is strongly contrasting from the previous level border hue
// 2) Stays away from the current level's piece palette hues
// The result is cached so it's deterministic and fast.
static uint16_t pickBorderHueForLevel(uint8_t level) {
  static uint16_t cache[256];
  static uint8_t computedUpTo = 0;

  if (level < 1) level = 1;
  if (level <= computedUpTo) return cache[level];

  // Golden-ratio-ish step: consecutive levels are always far apart in hue.
  // (Fixed-point: ~0.618 * 65536 ≈ 40503)
  static constexpr uint16_t PHI_STEP = 40503;

  // Candidate offsets to "dodge" the piece palette while preserving contrast.
  static constexpr uint16_t OFFS[8] = {
    0, 16384, 32768, 49152, 8192, 24576, 40960, 57344
  };

  for (uint16_t L = computedUpTo + 1; L <= level; L++) {
    uint16_t prevHue = (L > 1) ? cache[L - 1] : 0;

    uint16_t base = (uint16_t)((uint16_t)L * PHI_STEP);
    uint8_t tIdx = themeIndexStatic((uint8_t)L);

    uint16_t best = base;
    uint32_t bestScore = 0;

    // Targets (tuned for 16x16: vivid but not block-like)
    static constexpr uint16_t MIN_PREV = 21000;   // strong contrast from last level
    static constexpr uint16_t MIN_PAL  = 7000;    // keep away from piece hues

    for (uint8_t k = 0; k < 8; k++) {
      uint16_t h = (uint16_t)(base + OFFS[k]);

      // Distance to previous level hue (contrast)
      uint16_t dPrev = (L > 1) ? hueDistance(h, prevHue) : 65535;

      // Distance to piece palette hues (avoid conflicts)
      uint16_t dPal = 65535;
      for (uint8_t pid = 1; pid <= 7; pid++) {
        uint16_t ph = rgbToHue(THEMES[tIdx][pid]);
        uint16_t d = hueDistance(h, ph);
        if (d < dPal) dPal = d;
      }

      // Prefer meeting thresholds; otherwise still pick the best overall.
      uint32_t score = (uint32_t)dPal * 2u + (uint32_t)dPrev;
      bool ok = (dPrev >= MIN_PREV) && (dPal >= MIN_PAL);

      // Slight bias to prefer candidates that satisfy thresholds.
      if (ok) score += 300000u;

      if (score > bestScore) {
        bestScore = score;
        best = h;
      }
    }

    cache[L] = best;
  }

  computedUpTo = level;
  return cache[level];
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
  uint32_t now = millis();

  // Palette fade across the playfield
  g_isFading = true;
  g_fadeStartMs = now;
  g_fadeToLevel = nextLevel;
  g_fadeFromLevel = (nextLevel > 1) ? (uint8_t)(nextLevel - 1) : (uint8_t)1;

  // Border "waterfall" to celebrate the level transition
  g_waterfallActive = true;
  g_waterfallStartMs = now;
  g_waterfallFromLevel = g_fadeFromLevel;
  g_waterfallToLevel = g_fadeToLevel;
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

void MatrixDisplay::showNewHighScore(uint32_t score) {
  for (int i = 0; i < 6; i++) {
    strip_.fill(strip_.Color(150, 0, 0)); strip_.show(); delay(40);
    strip_.fill(strip_.Color(0, 150, 0)); strip_.show(); delay(40);
    strip_.fill(strip_.Color(0, 0, 150)); strip_.show(); delay(40);
  }
  strip_.clear();
  strip_.show();

  uint32_t cNew = strip_.Color(255, 0, 255);
  uint32_t cVal = strip_.Color(255, 255, 255);

  (void)showTwoLineTitleValue("NEW", String(score), cNew, cVal, 9000, nullptr);

  strip_.clear(); strip_.show(); delay(250);
}

void MatrixDisplay::showBootStats(uint32_t highScore, uint8_t highLevel, AbortFn abortFn) {
  uint32_t cHiLabel   = strip_.Color(255, 0, 255);
  uint32_t cHiVal     = strip_.Color(255, 255, 0);
  uint32_t cLvlLabel  = strip_.Color(0, 0, 255);
  uint32_t cLvlVal    = strip_.Color(255, 255, 255);

  if (showTwoLineTitleValue("HI", String(highScore), cHiLabel, cHiVal, 9000, abortFn)) return;

  uint32_t gapStart = millis();
  while ((millis() - gapStart) < 900) {
    if (abortFn && abortFn()) return;
    delay(15);
  }

  (void)showTwoLineTitleValue("LVL", String(highLevel), cLvlLabel, cLvlVal, 4500, abortFn);

  strip_.clear(); strip_.show();
  delay(200);
}

uint16_t MatrixDisplay::XY(uint8_t x, uint8_t y) const {
  if (x >= MATRIX_W || y >= MATRIX_H) return 0;
  uint8_t xx = (uint8_t)(MATRIX_W - 1 - x);
  uint8_t yy = MATRIX_BOTTOM_UP ? (MATRIX_H - 1 - y) : y;
  if (!SERPENTINE) return (uint16_t)yy * MATRIX_W + xx;
  if ((yy & 1) == 0) return (uint16_t)yy * MATRIX_W + xx;
  return (uint16_t)yy * MATRIX_W + (MATRIX_W - 1 - xx);
}

void MatrixDisplay::setPixel(int16_t x, int16_t y, uint32_t c) {
  if (x < 0 || x >= MATRIX_W || y < 0 || y >= MATRIX_H) return;
  strip_.setPixelColor(XY((uint8_t)x, (uint8_t)y), c);
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

uint32_t MatrixDisplay::arcadeBorderColor(const TetrisGame& g, uint8_t x, uint8_t y, uint32_t nowMs) const {
  uint16_t finalHue;

  if (g_isFading) {
    uint32_t elapsed = nowMs - g_fadeStartMs;
    if (elapsed > g_fadeDuration) elapsed = g_fadeDuration;

    uint32_t totalSteps = 14;
    uint32_t currentStep = (elapsed * totalSteps) / g_fadeDuration;
    if (currentStep >= totalSteps) currentStep = totalSteps - 1;

    uint8_t cIdx = currentStep % 7;
    uint8_t nextCIdx = (cIdx + 1) % 7;
    uint8_t tIdx = themeIndex(g_fadeToLevel);

    uint32_t c1 = THEMES[tIdx][cIdx + 1];
    uint32_t c2 = THEMES[tIdx][nextCIdx + 1];

    uint32_t stepDur = g_fadeDuration / totalSteps;
    uint32_t subTime = elapsed - ((currentStep * g_fadeDuration) / totalSteps);
    uint8_t splitY = (stepDur == 0) ? 0 : (uint8_t)((subTime * MATRIX_H) / stepDur);

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

uint32_t MatrixDisplay::solidBorderForLevel(uint8_t level, uint8_t bx, uint8_t by, uint32_t nowMs) const {
  if (level < 1) level = 1;

  uint8_t ring = 0;
  if (bx < BOARD_OFFSET_X) ring = bx;
  else ring = (uint8_t)(MATRIX_W - 1 - bx);
  if (ring > 2) ring = 2;

  const uint16_t hue = pickBorderHueForLevel(level);

  uint8_t sat = 255;
  uint8_t val = 92;

  if (ring == 1) { sat = 205; val = 68; }
  else if (ring == 2) { sat = 120; val = 38; }

  auto tri8 = [](uint8_t v) -> uint8_t {
    return (v & 0x80) ? (uint8_t)(255 - ((v & 0x7F) << 1)) : (uint8_t)((v & 0x7F) << 1);
  };

  // Faster + stronger motion so it’s *obviously* animated
  const uint8_t breathT = tri8((uint8_t)((nowMs >> 4) & 0xFF)); // ~4s
  const uint8_t rippleT = tri8((uint8_t)(((nowMs >> 2) + (by * 29) + (bx * 13)) & 0xFF)); // ~1s drift
  const int breath = (int)breathT - 128;
  const int ripple = (int)rippleT - 128;

  const int ampBreath = (ring == 0) ? 36 : (ring == 1) ? 24 : 7;
  const int ampRipple = (ring == 0) ? 30 : (ring == 1) ? 20 : 5;

  int dv = (breath * ampBreath) / 128 + (ripple * ampRipple) / 128;

  // Wider brightness window (still keeps inner ring subdued)
  int vMin = (ring == 0) ? 54  : (ring == 1) ? 34  : 22;
  int vMax = (ring == 0) ? 172 : (ring == 1) ? 128 : 60;

  int v = (int)val + dv;
  if (v < vMin) v = vMin;
  if (v > vMax) v = vMax;
  val = (uint8_t)v;

  // Stronger saturation shimmer (outer/middle only)
  if (ring != 2) {
    int ds = (ripple * ((ring == 0) ? 22 : 14)) / 128;
    int s = (int)sat + ds;
    if (s < 0) s = 0;
    if (s > 255) s = 255;
    sat = (uint8_t)s;
  }

  // Stronger hue shimmer (outer/middle only)
  uint16_t hue2 = hue;
  if (ring != 2) {
    int dh = (ripple * ((ring == 0) ? 1400 : 900)) / 128;
    hue2 = (uint16_t)(hue2 + dh);
  }

  // ✅ This is the “band” glint you were looking for
  if (ring == 0) {
    uint8_t band = (uint8_t)(((nowMs / 22) + (bx * 9) + (by * 5)) & 0xFF);

    if (band < 34) {
      int boost = (34 - band) * 3;
      int vv = (int)val + boost;
      if (vv > 190) vv = 190;
      val = (uint8_t)vv;
    } else if (band > 222) {
      int boost = (band - 222) * 3;
      int vv = (int)val + boost;
      if (vv > 190) vv = 190;
      val = (uint8_t)vv;
    }
  } else if (ring == 1) {
    uint8_t band = (uint8_t)(((nowMs / 28) + (bx * 7) + (by * 4)) & 0xFF);
    if (band < 30) {
      int boost = (30 - band) * 2;
      int vv = (int)val + boost;
      if (vv > 150) vv = 150;
      val = (uint8_t)vv;
    }
  }

  return strip_.ColorHSV(hue2, sat, val);
}

uint32_t MatrixDisplay::solidLevelBorderColor(const TetrisGame& g, uint8_t x, uint8_t y, uint32_t nowMs) const {
  return solidBorderForLevel(g.level(), x, y, nowMs);
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
  } else if (locked != g_prevLocked) {
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

  const bool chasingHighScore = g.allowHighScore() && (g.score() > g.highScore());

  int16_t wfFrontY = -999;
  if (g_waterfallActive) {
    uint32_t wfElapsed = (nowMs >= g_waterfallStartMs) ? (nowMs - g_waterfallStartMs) : g_waterfallDuration;
    if (wfElapsed >= g_waterfallDuration) {
      g_waterfallActive = false;
    } else {
      wfFrontY = (int16_t)(-2 + (int32_t)wfElapsed * (MATRIX_H + 4) / (int32_t)g_waterfallDuration);
    }
  }

  for (uint8_t y = 0; y < MATRIX_H; y++) {
    for (uint8_t x = 0; x < MATRIX_W; x++) {
      bool inBoardX = (x >= BOARD_OFFSET_X) && (x < (BOARD_OFFSET_X + BOARD_W));
      bool inBoardY = (y >= BOARD_OFFSET_Y) && (y < (BOARD_OFFSET_Y + BOARD_H));
      if (!inBoardX || !inBoardY) {
        uint32_t bc = chasingHighScore ? arcadeBorderColor(g, x, y, nowMs)
                                       : solidLevelBorderColor(g, x, y, nowMs);

        if (g_waterfallActive && wfFrontY > -900) {
          if (!chasingHighScore) {
            uint32_t oldC = solidBorderForLevel(g_waterfallFromLevel, x, y, nowMs);
            uint32_t newC = solidBorderForLevel(g_waterfallToLevel, x, y, nowMs);

            bc = ((int16_t)y <= wfFrontY) ? newC : oldC;

            int16_t dy = (int16_t)y - wfFrontY;
            if (dy == 0) bc = strip_.Color(255, 255, 255);
            else if (dy == 1 || dy == -1) bc = lerpColorRGB(bc, strip_.Color(255, 255, 255), 120);
          } else {
            int16_t dy = (int16_t)y - wfFrontY;
            if (dy == 0) bc = strip_.Color(255, 255, 255);
            else if (dy == 1 || dy == -1) bc = lerpColorRGB(bc, strip_.Color(255, 255, 255), 96);
            else if ((int16_t)y < wfFrontY) bc = lerpColorRGB(bc, strip_.Color(80, 80, 80), 40);
          }
        }

        setPixel(x, y, bc);
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
        if (lines >= 4) {
          c = strip_.ColorHSV((bx * 4000) + (nowMs * 60), 255, alpha);
        } else if (lines == 3) {
          if (elapsed < 80 || (elapsed >= 160 && elapsed < 240) || (elapsed >= 320 && elapsed < 400))
            c = strip_.Color(255, 255, 255);
          else
            c = scaleColor(c, alpha);
        } else if (lines == 2) {
          if (elapsed < 80 || (elapsed >= 160 && elapsed < 240))
            c = strip_.Color(255, 255, 255);
          else
            c = scaleColor(c, alpha);
        } else {
          if (elapsed < 60)
            c = strip_.Color(255, 255, 255);
          else
            c = scaleColor(c, alpha);
        }
      }

      setPixel(BOARD_OFFSET_X + bx, BOARD_OFFSET_Y + by, c);
    }
  }

  if (!g.isGameOver() && !clearing) {
    TetrisGame::Cell cells[4];
    uint8_t n = 0;
    g.getCurrentPieceBlocks(cells, n);
    uint8_t id = g.currentPieceId();

    uint32_t c;
    if (g_isFading) c = lerpColorRGB(pieceColor(g_fadeFromLevel, id), pieceColor(g_fadeToLevel, id), fadeStep);
    else c = pieceColor(lvl, id);

    for (uint8_t i = 0; i < n; i++) {
      setPixel(BOARD_OFFSET_X + cells[i].x, BOARD_OFFSET_Y + cells[i].y, c);
    }
  }

  if (g.isGameOver()) {
    strip_.fill(strip_.Color(20, 0, 0));
  }

  strip_.show();
}
