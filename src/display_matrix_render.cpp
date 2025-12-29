#include "display_matrix.h"
#include <Arduino.h>


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

// NOTE: These helpers were previously file-local in the single-file display implementation.
// They remain file-local here (render/theme logic only) to keep the public API unchanged.
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

  for (uint16_t L = (uint16_t)computedUpTo + 1; L <= level; L++) {
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
        // TETRIS (4 lines): crisp 4-flash strobe like the other clears,
        // with an animated rainbow running underneath between flashes.
        const uint32_t dur = g.clearDurationMs();
        const uint8_t flashes = 4;

        // Divide duration into on/off segments.
        uint32_t seg = (dur > 0) ? (dur / (flashes * 2)) : 1;
        if (seg < 1) seg = 1;

        uint32_t phase = elapsed / seg;
        const uint32_t maxPhase = (uint32_t)flashes * 2;
        if (phase >= maxPhase) phase = maxPhase - 1;
        const bool on = ((phase & 1u) == 0u);

        // Animated rainbow hue (moves during clear)
        uint16_t hue = (uint16_t)(
          (uint32_t)nowMs * 60UL +
          (uint32_t)bx * 5200UL +
          (uint32_t)by * 1100UL +
          (dur ? ((uint32_t)elapsed * 22000UL / dur) : 0)
        );

        // Keep brightness high until the tail, then fade out quickly (so it stays sharp).
        uint32_t tailStart = (dur * 3UL) / 4UL; // last 25% fades
        uint8_t vBase = 255;
        if (dur > 0 && elapsed > tailStart) {
          uint32_t tail = elapsed - tailStart;
          uint32_t tailDur = dur - tailStart;
          vBase = (tailDur == 0) ? 0 : (uint8_t)(255 - (tail * 255UL) / tailDur);
        }

        // Small sweep glint across X.
        uint8_t sweepX = (dur > 0)
          ? (uint8_t)((elapsed * (uint32_t)(BOARD_W - 1)) / dur)
          : 0;
        int d = (int)bx - (int)sweepX; if (d < 0) d = -d;
        uint8_t boost = (d == 0) ? 70 : (d == 1) ? 30 : (d == 2) ? 14 : 0;

        uint16_t v16 = (uint16_t)vBase + boost;
        if (v16 > 255) v16 = 255;

        uint32_t rainbow = strip_.ColorHSV(hue, 255, (uint8_t)v16);

        if (on) {
          // Flash white, but let a hint of the rainbow leak through.
          uint32_t white = strip_.ColorHSV(hue, 0, 255);
          c = lerpColorRGB(rainbow, white, 235);
        } else {
          c = rainbow;
        }
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
