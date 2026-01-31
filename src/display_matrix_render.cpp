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

// Border clock: decoupled from real millis() so the border flow can
// start slow and ramp up smoothly with level (never faster than original).
static uint32_t g_borderClockQ16 = 0;        // Q16.16 scaled milliseconds
static uint32_t g_borderClockLastRealMs = 0; // last real millis() used to advance the border clock
static uint32_t g_borderNowMs = 0;           // current border clock in milliseconds

static inline uint32_t borderSpeedQ16ForLevel(uint8_t level) {
  if (level < 1) level = 1;
  // Start slow, then ramp to 1.0 by this level.
  // (Cap at 1.0 so it can never be faster than the original animation.)
  static constexpr uint8_t  RAMP_MAX_LEVEL = 30;      // reach full speed by level 30
  static constexpr uint32_t MIN_SPEED_Q16  = 6554u;   // ~0.10 in Q16.16
  if (level >= RAMP_MAX_LEVEL) return 65536u;
  const uint32_t tQ16 = (uint32_t)(level - 1) * 65536u / (uint32_t)(RAMP_MAX_LEVEL - 1);
  return MIN_SPEED_Q16 + (uint32_t)(((uint64_t)(65536u - MIN_SPEED_Q16) * (uint64_t)tQ16) >> 16);
}


// Debug: force high-score border mode for testing
static bool     g_debugForceHighScoreBorders = false;
// Border reactive "sphere" around the falling piece (computed each frame in render()).
static bool     g_focusActive = false;
static int16_t  g_focusX = -1;        // matrix coordinates (0..MATRIX_W-1)
static int16_t  g_focusY = -1;        // matrix coordinates (0..MATRIX_H-1)
static uint8_t  g_focusStrength = 0;  // 0..255
static uint8_t  g_focusPieceId = 0;   // 1..7 (current falling piece), for border color influence

// Tuning knobs (border reaction)
static constexpr int     FOCUS_RADIUS = 9;           // pixels (matrix space)
static constexpr uint8_t FOCUS_BOOST_OUTER = 90;     // brightness boost at center (outer ring)
static constexpr uint8_t FOCUS_BOOST_MID   = 50;     // brightness boost at center (middle ring)
static constexpr uint8_t FOCUS_BOOST_INNER = 18;     // brightness boost at center (inner ring)
static constexpr uint16_t FOCUS_HUE_SHIFT_MAX = 700; // hue nudge amount near the piece
static constexpr uint8_t  FOCUS_DESAT_MAX = 35;      // desaturate a bit near the piece

// Level-up fade animation state
static bool     g_isFading = false;

// Set each frame: true when using high-score borders (rainbow), false for boss/normal.
static bool g_highScoreRainbowMode = false;
static bool g_bossLevelActive = false;
static uint8_t g_bossLevelNumber = 0;
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

// Level change overlay (number scrolling down the playfield)
static bool     g_levelOverlayActive = false;
static uint32_t g_levelOverlayStartMs = 0;
static uint32_t g_levelOverlayDuration = 1150;
static uint8_t  g_levelOverlayValue = 1;

// "Decade" milestone border reveal (border-only celebration when entering a new
// 10-level border style: levels 11/21/31/...)
static bool     g_milestoneBorderActive = false;
static uint32_t g_milestoneBorderStartMs = 0;
static uint32_t g_milestoneBorderDuration = MILESTONE_BORDER_REVEAL_MS;
static uint8_t  g_milestoneFromLevel = 1;
static uint8_t  g_milestoneToLevel = 1;

// 5x7 thin-line digit font used for in-game overlay (columns, LSB=top row)
static const uint8_t DIGITS_5x7[10][5] = {
  // Each entry is 5 columns, 7 rows (bit0 = row0/top).
  {0x3E, 0x41, 0x41, 0x41, 0x3E}, // 0
  {0x00, 0x42, 0x7F, 0x40, 0x00}, // 1
  {0x42, 0x61, 0x51, 0x49, 0x46}, // 2
  {0x22, 0x41, 0x49, 0x49, 0x36}, // 3
  {0x18, 0x14, 0x12, 0x7F, 0x10}, // 4
  {0x27, 0x45, 0x45, 0x45, 0x39}, // 5
  {0x3C, 0x4A, 0x49, 0x49, 0x30}, // 6
  {0x01, 0x71, 0x09, 0x05, 0x03}, // 7
  {0x36, 0x49, 0x49, 0x49, 0x36}, // 8
  {0x06, 0x49, 0x49, 0x29, 0x1E}, // 9
};


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

static constexpr uint8_t NUM_BORDER_STYLES = 4;

// Border animation changes every 10 levels:
//  1-10: Classic (current look)
// 11-20: Checkerboard contrast
// 21-30: Chain / double-helix (rounded links)
// 31-40: Wavy pulse
static inline uint8_t borderStyleForLevel(uint8_t level) {
  if (level < 1) level = 1;
  return (uint8_t)(((level - 1) / 10) % NUM_BORDER_STYLES);
}

static inline uint8_t hash8(uint32_t v) {
  // Cheap deterministic hash (good enough for twinkles/sparkles)
  v = v * 1103515245u + 12345u;
  v ^= (v >> 16);
  return (uint8_t)(v >> 24);
}

static inline uint16_t isqrt32(uint32_t x) {
  // Integer sqrt for small-ish values (fast, no floats).
  uint32_t op = x;
  uint32_t res = 0;
  uint32_t one = 1uL << 30; // second-to-top bit

  while (one > op) one >>= 2;

  while (one != 0) {
    if (op >= res + one) {
      op -= res + one;
      res = (res >> 1) + one;
    } else {
      res >>= 1;
    }
    one >>= 2;
  }
  return (uint16_t)res;
}

// Map any border pixel to a 1D index along a U-shaped path that hugs the
// playfield (left side -> bottom -> right side). Used for milestone reveals.
// Returns -1 for playfield pixels.
static inline int16_t milestoneBorderIndex(uint8_t x, uint8_t y) {
  // Border is everything NOT in the playfield (BOARD_OFFSET_X..+W-1, BOARD_OFFSET_Y..+H-1).
  const bool inBoardX = (x >= BOARD_OFFSET_X) && (x < (BOARD_OFFSET_X + BOARD_W));
  const bool inBoardY = (y >= BOARD_OFFSET_Y) && (y < (BOARD_OFFSET_Y + BOARD_H));
  if (inBoardX && inBoardY) return -1;

  // Bottom row adjacent to playfield.
  const uint8_t bottomY = (uint8_t)(BOARD_OFFSET_Y + BOARD_H);
  if (y == bottomY) {
    // Bottom segment: x spans the playfield width.
    if (x >= BOARD_OFFSET_X && x < (BOARD_OFFSET_X + BOARD_W)) {
      return (int16_t)(MATRIX_H + (x - BOARD_OFFSET_X)); // 16..25
    }
    // Left / right bottom corners clamp to the nearest endpoint.
    if (x < BOARD_OFFSET_X) return (int16_t)(MATRIX_H - 1);              // 15
    return (int16_t)(MATRIX_H + BOARD_W);                                // 26
  }

  // Left side: any of the left border columns map to the left leg.
  if (x < BOARD_OFFSET_X) {
    return (int16_t)y; // 0..15
  }

  // Right side: any of the right border columns map to the right leg (descending).
  if (x >= (BOARD_OFFSET_X + BOARD_W)) {
    return (int16_t)(MATRIX_H + BOARD_W + ((MATRIX_H - 1) - y)); // 26..41
  }

  // Top gap (where BOARD_OFFSET_Y==0, there is no top border across the playfield).
  return -1;
}

static inline uint8_t milestoneDepth(uint8_t x) {
  // 0 = inner edge of the playfield border (closest to pieces)
  // 1/2 = farther out; we attenuate brightness so the celebration stays subtle.
  if (x < BOARD_OFFSET_X) {
    uint8_t d = (uint8_t)((BOARD_OFFSET_X - 1) - x); // x=2->0, x=1->1, x=0->2
    if (d > 2) d = 2;
    return d;
  }
  if (x >= (BOARD_OFFSET_X + BOARD_W)) {
    uint8_t d = (uint8_t)(x - (BOARD_OFFSET_X + BOARD_W)); // x=13->0,14->1,15->2
    if (d > 2) d = 2;
    return d;
  }
  return 0;
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


void MatrixDisplay::levelTransition(uint8_t fromLevel, uint8_t toLevel) {
  if (fromLevel < 1) fromLevel = 1;
  if (toLevel < 1) toLevel = 1;

  uint32_t now = millis();

  // Palette fade across the playfield
  g_isFading = true;
  g_fadeStartMs = now;
  g_fadeFromLevel = fromLevel;
  g_fadeToLevel = toLevel;

  // Border "waterfall" to celebrate the level transition
  g_waterfallActive = true;
  g_waterfallStartMs = now;
  g_waterfallFromLevel = fromLevel;
  g_waterfallToLevel = toLevel;

  // "Decade" milestone: when entering a new 10-level border style (11/21/31/...)
  // run an additional border-only reveal/chase so the new theme reads clearly
  // without touching the playfield.
  if (MILESTONE_BORDER_REVEAL_ENABLED && toLevel > 1 && ((toLevel % 10) == 1)) {
    g_milestoneBorderActive = true;
    g_milestoneBorderStartMs = now;
    g_milestoneFromLevel = fromLevel;
    g_milestoneToLevel = toLevel;
  } else {
    g_milestoneBorderActive = false;
  }

  // In-game overlay (optional): scroll the new level number down the playfield.
  if (LEVEL_NUMBER_DROPDOWN_ENABLED) {
    g_levelOverlayActive = true;
    g_levelOverlayStartMs = now;
    g_levelOverlayValue = toLevel;
  } else {
    g_levelOverlayActive = false;
  }
}

void MatrixDisplay::setDebugForceHighScoreBorders(bool enable) {
  g_debugForceHighScoreBorders = enable;
}

void MatrixDisplay::levelUpFlash(uint8_t nextLevel) {
  uint8_t from = (nextLevel > 1) ? (uint8_t)(nextLevel - 1) : (uint8_t)1;
  levelTransition(from, nextLevel);
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

  const uint32_t bt = g_borderNowMs;

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
    uint32_t dt = (bt >= g_wipeStartMs) ? (bt - g_wipeStartMs) : 0;
    uint8_t splitY = (dt >= duration) ? MATRIX_H : (uint8_t)((dt * MATRIX_H) / duration);

    uint16_t baseHue = (y < splitY) ? g_hueNew : g_hueOld;
    uint16_t rawScroll = (uint16_t)((x + y) * 120 - (bt * 30));
    finalHue = baseHue + (rawScroll >> 5);
  }

  bool isMeshGap = ((x ^ y) & 1);

    uint8_t sat = 200;
  uint8_t val = isMeshGap ? 40 : 140;

  // High-score borders are rainbow and should NOT depend on the locked-piece palette.
  // Boss borders reuse the classic arcade hue behavior (piece-linked wipe).
  uint16_t baseHue = finalHue;
  if (g_highScoreRainbowMode) {
    baseHue = (uint16_t)(
      (uint32_t)bt * 28UL +
      (uint32_t)x * 5200UL +
      (uint32_t)y * 3100UL
    );
  }

  uint32_t base = strip_.ColorHSV(baseHue, sat, val);

  // High-score mode: continuous rainbow irradiation from the falling piece.
  if (g_highScoreRainbowMode && g_focusActive && g_focusStrength) {
    // How close is the piece to the nearest matrix edge?
    int minEdge = (int)g_focusX;
    int t = (int)(MATRIX_W - 1) - (int)g_focusX; if (t < minEdge) minEdge = t;
    t = (int)g_focusY; if (t < minEdge) minEdge = t;
    t = (int)(MATRIX_H - 1) - (int)g_focusY; if (t < minEdge) minEdge = t;

    // 0 at edge => strongest. Past ~7 px from edge => mostly off.
    int edgeFactor = 255 - minEdge * 36;
    if (edgeFactor < 0) edgeFactor = 0;
    if (edgeFactor > 255) edgeFactor = 255;

    // Distance from this border pixel to the piece center (matrix coords).
    int dx = (int)x - (int)g_focusX;
    int dy = (int)y - (int)g_focusY;
    uint32_t d2 = (uint32_t)(dx * dx + dy * dy);
    uint16_t dist = isqrt32(d2); // 0..~32 on 16x16

    // Per-pixel falloff: nearest border side lights up most, far side least.
    int near = 255 - (int)dist * 18;
    if (near < 0) near = 0;

    uint16_t mix16 = (uint16_t)((uint32_t)edgeFactor * (uint32_t)near / 255u);
    mix16 = (uint16_t)((uint32_t)mix16 * (uint32_t)g_focusStrength / 255u);
    uint8_t mix = (uint8_t)mix16;

    if (mix) {
      auto tri8 = [](uint8_t v) -> uint8_t {
        return (v & 0x80) ? (uint8_t)(255 - ((v & 0x7F) << 1)) : (uint8_t)((v & 0x7F) << 1);
      };

      const uint8_t wave = tri8((uint8_t)((dist * 20u - (uint32_t)(bt / 10u)) & 0xFF));

      uint16_t hue = (uint16_t)(
        (uint32_t)bt * 55UL +
        (uint32_t)dist * 11000UL +
        (uint32_t)x * 900UL +
        (uint32_t)y * 700UL
      );

      uint8_t rVal = (uint8_t)(140 + ((uint16_t)wave * 115u) / 255u);
      uint32_t rainbow = strip_.ColorHSV(hue, 255, rVal);

      return lerpColorRGB(base, rainbow, mix);
    }
  }

  return base;
}


uint32_t MatrixDisplay::solidBorderForLevel(uint8_t level, uint8_t bx, uint8_t by, uint32_t nowMs) const {
  if (level < 1) level = 1;

  const uint32_t bt = g_borderNowMs;
  (void)nowMs; // border animation uses g_borderNowMs (scaled time)

  uint8_t ring = 0;
  if (bx < BOARD_OFFSET_X) ring = bx;
  else ring = (uint8_t)(MATRIX_W - 1 - bx);
  if (ring > 2) ring = 2;

  // Level 1: keep the *same* motion/animation as other levels, but render it plain (grayscale)
  // so level 2 feels like a color reward.
  const bool mutedFirstLevel = (level == 1);

  uint8_t style = borderStyleForLevel(level);

  uint16_t hue = mutedFirstLevel ? 0 : pickBorderHueForLevel(level);

  // Boss level (just before pattern change): preview the NEXT border style, but with
  // the per-locked-piece color wipe ("color changes") to give a final-boss vibe.
  const bool bossThisLevel = g_bossLevelActive && (level == g_bossLevelNumber);
  if (bossThisLevel) {
    style = borderStyleForLevel((uint8_t)(level + 1));
    const uint32_t duration = 600;
    uint32_t dt = (bt >= g_wipeStartMs) ? (bt - g_wipeStartMs) : 0;
    uint8_t splitY = (dt >= duration) ? MATRIX_H : (uint8_t)((dt * MATRIX_H) / duration);
    hue = (by < splitY) ? g_hueNew : g_hueOld;
  }

  uint8_t sat = 255;
  uint8_t val = 92;

  if (ring == 1) { sat = 205; val = 68; }
  else if (ring == 2) { sat = 120; val = 38; }

  auto tri8 = [](uint8_t v) -> uint8_t {
    return (v & 0x80) ? (uint8_t)(255 - ((v & 0x7F) << 1)) : (uint8_t)((v & 0x7F) << 1);
  };

  // Faster + stronger motion so it’s *obviously* animated
  const uint8_t breathT = tri8((uint8_t)((bt >> 4) & 0xFF)); // ~4s
  const uint8_t rippleT = tri8((uint8_t)(((bt >> 2) + (by * 29) + (bx * 13)) & 0xFF)); // ~1s drift
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

  // Animation style changes every 10 levels. First 10 levels keep the "classic" look.
  if (style == 1) {
    // STYLE 1 (11-20): "Checkerboard Contrast" — alternating tiles brighten/dim.
    // Uses 2x2 tiles for a clear pattern on low resolution, with a slow phase flip.
    const uint8_t phase = (uint8_t)((bt / 1200UL) & 1UL); // ~1.2s invert
    const bool on = ((((bx >> 1) ^ (by >> 1) ^ phase) & 1u) == 0u);

    if (on) {
      int vv = (int)val + ((ring == 0) ? 52 : (ring == 1) ? 32 : 12);
      if (vv > vMax) vv = vMax;
      val = (uint8_t)vv;

      if (ring != 2) {
        int ss = (int)sat + ((ring == 0) ? 18 : 12);
        if (ss > 255) ss = 255;
        sat = (uint8_t)ss;
      }
    } else {
      int vv = (int)val - ((ring == 0) ? 42 : (ring == 1) ? 26 : 10);
      if (vv < vMin) vv = vMin;
      val = (uint8_t)vv;

      if (ring == 0) sat = (uint8_t)(((uint16_t)sat * 165u) / 255u);
      else if (ring == 1) sat = (uint8_t)(((uint16_t)sat * 190u) / 255u);

      if (ring == 0) hue2 = (uint16_t)(hue2 + 12000u);
      else if (ring == 1) hue2 = (uint16_t)(hue2 + 8000u);
    }
  } else if (style == 2) {
    // STYLE 2 (21-30): "Chain / Double-Helix" — rounded links that alternate
    // between the outer+middle rings and the middle+inner rings.
    //
    // This reads as a scrolling chain (or braided helix) even on a 16x16.
    const int16_t idx = milestoneBorderIndex(bx, by);
    const uint8_t uIdx = (idx >= 0) ? (uint8_t)idx : (uint8_t)((bx + by) & 0xFF);

    // Tunables:
    // - LINK_LEN controls link size (path pixels per half-link). 7 gives 3 full links
    //   around the 42px U-path on a 16x16.
    // - scroll speed uses border-time so it naturally ramps with level.
    static constexpr uint8_t LINK_LEN = 7;
    const uint8_t scroll = (uint8_t)((bt >> 8) & 0xFF); // ~1 step / 256ms at full speed

    uint8_t local = (uint8_t)(uIdx + scroll);
    local = (uint8_t)(local % (uint8_t)(2u * LINK_LEN));
    const bool innerLink = (local >= LINK_LEN);
    local = (uint8_t)(local % LINK_LEN);

    // local: 0..LINK_LEN-1 -> 0..255
    const uint8_t t = (LINK_LEN <= 1)
                        ? 0
                        : (uint8_t)(((uint16_t)local * 255u) / (uint16_t)(LINK_LEN - 1));

    const uint8_t center = tri8(t);              // bright in the middle of the link
    const uint8_t ends   = (uint8_t)(255 - center); // bright at the ends

    // Build a rounded "loop": ends on one ring, belly on the adjacent ring.
    uint8_t intensity = 0;
    if (!innerLink) {
      // Outer loop (like a chain link sitting "outside")
      if (ring == 0) intensity = ends;
      else if (ring == 1) intensity = center;
      else intensity = 14;
    } else {
      // Inner loop (alternating link)
      if (ring == 2) intensity = ends;
      else if (ring == 1) intensity = center;
      else intensity = 14;
    }

    // Map intensity -> value. Give this decade a wider brightness window so it
    // reads clearly as a new, distinct border.
    int vmax2 = vMax + ((ring == 0) ? 24 : (ring == 1) ? 30 : 55);
    if (vmax2 > 220) vmax2 = 220;

    int vv = vMin + (int)(((uint16_t)intensity * (uint16_t)(vmax2 - vMin)) / 255u);
    if (vv < vMin) vv = vMin;
    if (vv > vmax2) vv = vmax2;
    val = (uint8_t)vv;

    // Slight saturation and hue twist to help the links feel "round".
    if (ring != 2) {
      int ds = ((int)intensity - 128) * 18 / 128;
      int s = (int)sat + ds;
      if (s < 0) s = 0;
      if (s > 255) s = 255;
      sat = (uint8_t)s;

      if (!mutedFirstLevel) {
        int dh = innerLink ? 1800 : -1800;
        hue2 = (uint16_t)(hue2 + dh);
      }
    } else {
      // Keep the innermost ring a touch less saturated for depth.
      sat = (uint8_t)(((uint16_t)sat * 210u) / 255u);
    }
  } else if (style == 3) {
    // STYLE 3 (31-40): "Wavy pulse" — a traveling wave that shifts brightness + hue.
    const int wA = (int)tri8((uint8_t)(((bt >> 3) + (by * 13)) & 0xFF)) - 128;
    const int wB = (int)tri8((uint8_t)(((bt >> 2) + (by * 7) + (bx * 19)) & 0xFF)) - 128;

    int dvw = (wA * ((ring == 0) ? 26 : (ring == 1) ? 18 : 6)) / 128 +
              (wB * ((ring == 0) ? 18 : (ring == 1) ? 12 : 4)) / 128;

    int vv = (int)val + dvw;
    if (vv < vMin) vv = vMin;
    if (vv > vMax) vv = vMax;
    val = (uint8_t)vv;

    if (ring != 2) {
      int dh = (wA * ((ring == 0) ? 2200 : 1400)) / 128;
      hue2 = (uint16_t)(hue2 + dh);

      int ds = (wB * ((ring == 0) ? 26 : 16)) / 128;
      int s = (int)sat + ds;
      if (s < 0) s = 0;
      if (s > 255) s = 255;
      sat = (uint8_t)s;
    }
  }

  uint8_t focusColorBlend = 0;

  // Reactive "sphere of light" around the falling piece.
  // Border pixels near the active piece get a gentle brightness lift, slight desaturation,
  // and a tiny hue nudge (so it reads as interactive, but stays subtle).
  if (((level % 10) == 0) && g_focusActive && g_focusStrength) {
    int dx = (int)bx - (int)g_focusX;
    int dy = (int)by - (int)g_focusY;
    int d2 = dx * dx + dy * dy;
    const int r2 = FOCUS_RADIUS * FOCUS_RADIUS;

    if (d2 < r2) {
      uint16_t inten = (uint16_t)(((r2 - d2) * 255) / r2); // 0..255
      inten = (uint16_t)((inten * g_focusStrength) / 255);

      const uint8_t ampV = (ring == 0) ? FOCUS_BOOST_OUTER : (ring == 1) ? FOCUS_BOOST_MID : FOCUS_BOOST_INNER;

      int vv = (int)val + ((int)inten * (int)ampV) / 255;
      if (vv > 235) vv = 235;
      val = (uint8_t)vv;

      // Desaturate slightly as it brightens (looks like a glow).
      if (ring != 2) {
        int ss = (int)sat - ((int)inten * (int)FOCUS_DESAT_MAX) / 255;
        if (ss < 0) ss = 0;
        sat = (uint8_t)ss;
      }

      // Tiny hue nudge: bias by left/right side of the piece.
      if (!mutedFirstLevel && ring != 2) {
        int dir = (dx >= 0) ? 1 : -1;
        int dh = dir * ((int)inten * (int)FOCUS_HUE_SHIFT_MAX) / 255;
        hue2 = (uint16_t)(hue2 + dh);
      }

      // After decade 2 (levels 21+), let the falling piece strongly tint nearby border pixels.
      // This makes the border feel more responsive and color-linked to gameplay.
      if (!mutedFirstLevel && level >= 21 && g_focusPieceId) {
        const uint8_t maxBlend = (ring == 0) ? 180 : (ring == 1) ? 130 : 60;
        uint16_t cb = (uint16_t)(((uint32_t)inten * (uint32_t)maxBlend) / 255UL); // 0..maxBlend
        if (cb > 255) cb = 255;
        focusColorBlend = (uint8_t)cb;
      }
    }
  }

// If this is level 1, force grayscale while keeping the same brightness motion + glints.
  if (mutedFirstLevel) {
    sat = 0;
    hue2 = 0;
  }

  // ✅ This is the “band” glint you were looking for
  // For the chain/helix decade (style 2), we skip this so the rounded links read
  // more clearly and feel more "distinct" from the first two borders.
  if (style != 2) {
    if (ring == 0) {
      uint8_t band = (uint8_t)(((bt / 22) + (bx * 9) + (by * 5)) & 0xFF);

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
      uint8_t band = (uint8_t)(((bt / 28) + (bx * 7) + (by * 4)) & 0xFF);
      if (band < 30) {
        int boost = (30 - band) * 2;
        int vv = (int)val + boost;
        if (vv > 150) vv = 150;
        val = (uint8_t)vv;
      }
    }
  }

  uint32_t out = strip_.ColorHSV(hue2, sat, val);
  if (focusColorBlend && g_focusPieceId && !mutedFirstLevel) {
    uint32_t pc = pieceColor(level, g_focusPieceId);
    out = lerpColorRGB(out, pc, focusColorBlend);
  }
  return out;
}


uint32_t MatrixDisplay::solidLevelBorderColor(const TetrisGame& g, uint8_t x, uint8_t y, uint32_t nowMs) const {
  return solidBorderForLevel(g.level(), x, y, nowMs);
}


void MatrixDisplay::render(const TetrisGame& g, uint32_t nowMs) {
  strip_.clear();

  const uint8_t lvl = g.level();

  // Update border time (smooth, uninterrupted). This ramps from slow -> current
  // speed with level, but is capped so it can never run faster than the original.
  uint32_t lastReal = g_borderClockLastRealMs;
  if (lastReal == 0 || nowMs < lastReal) lastReal = nowMs;
  const uint32_t dReal = nowMs - lastReal;
  g_borderClockLastRealMs = nowMs;
  const uint32_t spQ16 = borderSpeedQ16ForLevel(lvl);
  g_borderClockQ16 += (uint64_t)dReal * (uint64_t)spQ16;
  g_borderNowMs = (uint32_t)(g_borderClockQ16 >> 16);
  uint8_t tIdx = themeIndex(lvl);
  uint8_t locked = g.lastLockedPieceType();

  if (g_firstRun) {
    g_prevLocked = locked;
    g_hueNew = rgbToHue(THEMES[tIdx][locked + 1]) + 32768;
    g_hueOld = g_hueNew;
    g_wipeStartMs = g_borderNowMs;
    g_firstRun = false;
  } else if (locked != g_prevLocked) {
    // Update the boss-level wipe hues when a piece locks, but DON'T restart the
    // wipe timer mid-flight. Restarting looks like a border 'glitch' when pieces
    // lock quickly back-to-back.
    const uint16_t newHue = rgbToHue(THEMES[tIdx][locked + 1]) + 32768;

    const uint32_t WIPE_DUR_MS = 600;
    uint32_t dt = (g_borderNowMs >= g_wipeStartMs) ? (g_borderNowMs - g_wipeStartMs) : WIPE_DUR_MS;

    // If the previous wipe is done, start a fresh one; otherwise just retarget the
    // 'new' hue and let the current wipe continue smoothly.
    if (dt >= WIPE_DUR_MS) {
      g_hueOld = g_hueNew;
      g_wipeStartMs = g_borderNowMs;
    }
    g_hueNew = newHue;
    g_prevLocked = locked;
  }

  uint8_t fadeStep = 255;
  if (g_isFading) {
    uint32_t elapsed = nowMs - g_fadeStartMs;
    if (elapsed >= g_fadeDuration) g_isFading = false;
    else fadeStep = (uint8_t)((elapsed * 255) / g_fadeDuration);
  }

  // "Record chase" mode:
  // We only show the special arcade/rainbow border during the *last 3 lines*
  // before you would surpass your stored max level (i.e., while you're still
  // at maxLevel, and you're 1-3 line clears away from leveling up).
  //
  // This keeps the border mostly "normal" and adds tension only near the
  // breakthrough moment.
  const uint8_t linesIntoLevel = (uint8_t)(g.lines() % 10);
  const uint8_t linesToNextLevel = (uint8_t)(10 - linesIntoLevel); // 10..1

  const bool chasingMaxLevel = (g.maxLevel() < 99) && (g.level() == g.maxLevel());
  const bool nearBreakthrough = (linesToNextLevel <= 3);

  const bool highScoreBorders = g_debugForceHighScoreBorders ||
                               (g.hasPlayedBefore() && g.allowHighScore() && chasingMaxLevel && nearBreakthrough);
  const bool bossLevel = (!highScoreBorders) && ((lvl % 10) == 0);
  g_highScoreRainbowMode = highScoreBorders;
  g_bossLevelActive = bossLevel;
  g_bossLevelNumber = lvl;


  // Clearing state is needed for both playfield rendering and border glow behavior.
  const bool clearing = g.isClearingLines();
  const uint32_t elapsed = clearing ? g.clearingElapsedMs(nowMs) : 0;
  const uint8_t alphaLin = g.clearingAlpha(nowMs);
  const uint8_t alpha = (uint8_t)(((uint16_t)alphaLin * (uint16_t)alphaLin) >> 8);

  // Compute a focus point from the current falling piece so borders can react to it.
  // NOTE: This must happen BEFORE we render border pixels.
  g_focusActive = false;
  g_focusStrength = 0;
  g_focusX = -1;
  g_focusY = -1;
  g_focusPieceId = 0;

  if (!g.isGameOver() && !clearing) {
    TetrisGame::Cell fcells[4];
    uint8_t fn = 0;
    g.getCurrentPieceBlocks(fcells, fn);

    if (fn > 0) {
      int sumx = 0;
      int sumy = 0;
      for (uint8_t i = 0; i < fn; i++) {
        sumx += fcells[i].x;
        sumy += fcells[i].y;
      }

      // Convert from board coords -> matrix coords
      g_focusX = (int16_t)(BOARD_OFFSET_X + (sumx / (int)fn));
      g_focusY = (int16_t)(BOARD_OFFSET_Y + (sumy / (int)fn));

      g_focusActive = true;
      g_focusStrength = 255;
      g_focusPieceId = g.currentPieceId();
    }
  }

  int16_t wfFrontY = -999;
  if (g_waterfallActive) {
    uint32_t wfElapsed = (nowMs >= g_waterfallStartMs) ? (nowMs - g_waterfallStartMs) : 0;
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
        uint32_t bc = highScoreBorders ? arcadeBorderColor(g, x, y, nowMs)
                                       : solidLevelBorderColor(g, x, y, nowMs);

        if (g_waterfallActive && wfFrontY > -900) {
          if (!highScoreBorders) {
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

        // ------------------------------------------------------------
        // Decade milestone border reveal (levels 11/21/31/...)
        // Border-only celebration: a short palette chase that hugs the
        // playfield border, so it reads as a "new theme" without interfering
        // with piece readability.
        // ------------------------------------------------------------
        if (!highScoreBorders && g_milestoneBorderActive) {
          uint32_t msElapsed = (nowMs >= g_milestoneBorderStartMs) ? (nowMs - g_milestoneBorderStartMs) : 0;
          if (msElapsed >= g_milestoneBorderDuration) {
            g_milestoneBorderActive = false;
          } else {
            const int16_t idx = milestoneBorderIndex(x, y);
            if (idx >= 0) {
              const int16_t pathLen = (int16_t)(MATRIX_H + BOARD_W + MATRIX_H); // 42 on 16x16 + 10-wide board
              int16_t head = (int16_t)((msElapsed * (uint32_t)(pathLen - 1)) / g_milestoneBorderDuration);
              if (head < 0) head = 0;
              if (head > (pathLen - 1)) head = (int16_t)(pathLen - 1);

              // Tail intensity (only behind the head, so it feels like a reveal).
              const int16_t tail = 10;
              int16_t d = (int16_t)(head - idx);
              uint8_t a = 0;
              if (d >= 0 && d <= tail) {
                uint16_t aa = (uint16_t)(255u - (uint32_t)d * 255u / (uint32_t)tail);
                // Keep this subtle: cap at ~190.
                aa = (uint16_t)((aa * 190u) / 255u);
                a = (uint8_t)aa;
              } else if (idx == (int16_t)(head + 1)) {
                // Tiny leading sparkle.
                a = 40;
              }

              // Attenuate for farther-out border columns so it doesn't pull focus.
              const uint8_t depth = milestoneDepth(x);
              if (depth == 1) a = (uint8_t)(((uint16_t)a * 175u) / 255u);
              else if (depth == 2) a = (uint8_t)(((uint16_t)a * 125u) / 255u);

              if (a) {
                // Build an accent that represents the *new* theme:
                // - Primary: new border hue (very readable as a "theme" change)
                // - Secondary: new piece palette color (adds richness without touching the playfield)
                const uint16_t bh = pickBorderHueForLevel(g_milestoneToLevel);
                const uint32_t borderAccent = strip_.ColorHSV(bh, 255, 255);
                const uint8_t pid = (uint8_t)(1 + ((uint8_t)(idx / 2) % 7));
                const uint32_t pieceAccent = pieceColor(g_milestoneToLevel, pid);
                uint32_t accent = lerpColorRGB(borderAccent, pieceAccent, 120);

                if (idx == head) {
                  accent = lerpColorRGB(accent, strip_.Color(255, 255, 255), 70);
                }

                bc = lerpColorRGB(bc, accent, a);
              }

              // Quick corner wink at the start (border-only, minimal distraction).
              if (msElapsed < 260) {
                const bool on = (((msElapsed / 65u) & 1u) == 0u);
                if (on) {
                  const uint8_t lx = (uint8_t)(BOARD_OFFSET_X - 1);
                  const uint8_t rx = (uint8_t)(BOARD_OFFSET_X + BOARD_W);
                  const uint8_t byy = (uint8_t)(BOARD_OFFSET_Y + BOARD_H);
                  const bool corner = ((x == lx || x == rx) && (y == 0 || y == byy));
                  if (corner) {
                    bc = lerpColorRGB(bc, strip_.Color(255, 255, 255), 180);
                  }
                }
              }
            }
          }
        }

        setPixel(x, y, bc);
      }
    }
  }

  auto b = g.board();

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

  // ------------------------------------------------------------
  // Level overlay: scroll the NEW level number down the playfield.
  // Drawn last (after blocks) so it remains visible even when the stack is high.
  // We "stencil" the number by DIMMING whatever pixels it touches.
  // This keeps the overlay subtle and avoids looking like a solid block.
  // ------------------------------------------------------------
  if (LEVEL_NUMBER_DROPDOWN_ENABLED && g_levelOverlayActive && !g.isGameOver()) {
    uint32_t elapsedO = (nowMs >= g_levelOverlayStartMs) ? (nowMs - g_levelOverlayStartMs) : 0;
    const bool doneO = (elapsedO >= g_levelOverlayDuration);
    const uint32_t tO = doneO ? g_levelOverlayDuration : elapsedO;

    {
      const uint8_t value = g_levelOverlayValue;

      // Thin, higher-detail digits for the in-game level overlay.
      // Two-digit levels fill the full 10-wide playfield; single-digit levels are centered.
      const int digitW = 5;
      const int digitH = 7;
      const bool twoDigits = (value >= 10);
      const int totalW = twoDigits ? (digitW * 2) : digitW;

      // Scroll the digit TOP from above the field to a fully-visible "bottom aligned" position.
      // (This prevents the digit from being mostly clipped at the end, which looks like it vanishes early.)
      const int16_t startY = (int16_t)BOARD_OFFSET_Y - digitH - 1;
      int16_t endY = (int16_t)BOARD_OFFSET_Y + (int16_t)BOARD_H - (int16_t)digitH;
      if (endY < (int16_t)BOARD_OFFSET_Y) endY = (int16_t)BOARD_OFFSET_Y;

      const int16_t yPos = (int16_t)(startY + (int32_t)tO * (int32_t)(endY - startY) / (int32_t)g_levelOverlayDuration);

      // Fade a bit as it travels down (but keep it punchy for readability).
      // Fade as it travels down; keep it subtle but still visible.
      int a = 160 - (int)((tO * 70UL) / g_levelOverlayDuration);
      if (a < 90) a = 90;
      if (a > 160) a = 160;
      const uint8_t alphaO = (uint8_t)a;

      int16_t x0 = (int16_t)(BOARD_OFFSET_X + (int16_t)((BOARD_W - totalW) / 2));
      if (x0 < (int16_t)BOARD_OFFSET_X) x0 = (int16_t)BOARD_OFFSET_X;

      // Stencil effect: if the underlying pixel is bright, dim it; if it's near-black, lift it slightly.
      // This keeps the number visible even over empty space, while still "interacting" with blocks.
      auto stencilAt = [&](int16_t mx, int16_t my, uint8_t strength) {
        if (mx < (int16_t)BOARD_OFFSET_X || mx >= (int16_t)(BOARD_OFFSET_X + BOARD_W)) return;
        if (my < (int16_t)BOARD_OFFSET_Y || my >= (int16_t)(BOARD_OFFSET_Y + BOARD_H)) return;
        if (mx < 0 || mx >= (int16_t)MATRIX_W || my < 0 || my >= (int16_t)MATRIX_H) return;

        uint16_t idx2 = XY((uint8_t)mx, (uint8_t)my);
        uint32_t old = strip_.getPixelColor(idx2);
        uint8_t r = (uint8_t)((old >> 16) & 0xFF);
        uint8_t gch = (uint8_t)((old >> 8) & 0xFF);
        uint8_t bch = (uint8_t)(old & 0xFF);

        uint16_t lum = (uint16_t)r + (uint16_t)gch + (uint16_t)bch;

        uint8_t nr, ng, nb;
        if (lum < 80) {
          // Background is dark/empty: lift it to a faint neutral gray so the digit is visible
          // even over empty space, while staying subtle.
          //
          // Map strength to a small floor of brightness.
          const uint8_t lift = (uint8_t)(14u + (strength / 5u));
          nr = (r   < lift) ? lift : r;
          ng = (gch < lift) ? lift : gch;
          nb = (bch < lift) ? lift : bch;
        } else {
          // Bright pixel (block): dim it so the number "cuts" into the stack.
          uint16_t keep = (uint16_t)(255u - strength);
          nr = (uint8_t)(((uint16_t)r * keep) / 255u);
          ng = (uint8_t)(((uint16_t)gch * keep) / 255u);
          nb = (uint8_t)(((uint16_t)bch * keep) / 255u);
        }

        strip_.setPixelColor(idx2, strip_.Color(nr, ng, nb));
      };

// Keep it 1-pixel and avoid any "filled" background: we only touch pixels that
      // are part of the digit strokes (only digit strokes), so it never looks like a block.
      auto drawDigit = [&](uint8_t digit, int16_t dx, int16_t dy, uint8_t alpha) {
        if (digit > 9) return;
        for (int col = 0; col < 5; col++) {
          uint8_t bits = DIGITS_5x7[digit][col];
          for (int row = 0; row < 7; row++) {
            if ((bits >> row) & 1u) {
              // Stencil: dim the pixels the digit touches.
              stencilAt((int16_t)(dx + col), (int16_t)(dy + row), alpha);
            }
          }
        }
      };

      auto drawNumber = [&](uint8_t v, int16_t dx, int16_t dy, uint8_t alpha) {
        if (v > 99) v = 99;
        uint8_t d0 = (uint8_t)(v / 10);
        uint8_t d1 = (uint8_t)(v % 10);
        if (v < 10) {
          drawDigit(d1, dx, dy, alpha);
        } else {
          drawDigit(d0, dx, dy, alpha);
          drawDigit(d1, (int16_t)(dx + digitW), dy, alpha);
        }
      };
      // Main strokes
      drawNumber(value, x0, yPos, alphaO);
    }

    // Turn the overlay off only AFTER we have rendered the final frame (bottom aligned).
    if (doneO) {
      g_levelOverlayActive = false;
    }
  }

  if (g.isGameOver()) {
    strip_.fill(strip_.Color(20, 0, 0));
  }

  strip_.show();
}
