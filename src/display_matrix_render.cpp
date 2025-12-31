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


// Debug: force high-score border mode for testing
static bool     g_debugForceHighScoreBorders = false;
// Border reactive "sphere" around the falling piece (computed each frame in render()).
static bool     g_focusActive = false;
static int16_t  g_focusX = -1;        // matrix coordinates (0..MATRIX_W-1)
static int16_t  g_focusY = -1;        // matrix coordinates (0..MATRIX_H-1)
static uint8_t  g_focusStrength = 0;  // 0..255

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
// 11-20: Comet scan
// 21-30: Sparkle twinkle
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

  // In-game overlay: scroll the new level number down the playfield.
  g_levelOverlayActive = true;
  g_levelOverlayStartMs = now;
  g_levelOverlayValue = toLevel;
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
    uint32_t dt = (nowMs >= g_wipeStartMs) ? (nowMs - g_wipeStartMs) : 0;
    uint8_t splitY = (dt >= duration) ? MATRIX_H : (uint8_t)((dt * MATRIX_H) / duration);

    uint16_t baseHue = (y < splitY) ? g_hueNew : g_hueOld;
    uint16_t rawScroll = (uint16_t)((x + y) * 120 - (nowMs * 30));
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
      (uint32_t)nowMs * 28UL +
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

      const uint8_t wave = tri8((uint8_t)((dist * 20u - (uint32_t)(nowMs / 10u)) & 0xFF));

      uint16_t hue = (uint16_t)(
        (uint32_t)nowMs * 55UL +
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
    uint32_t dt = (nowMs >= g_wipeStartMs) ? (nowMs - g_wipeStartMs) : 0;
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

  // Animation style changes every 10 levels. First 10 levels keep the "classic" look.
  if (style == 1) {
    // STYLE 1 (11-20): "Corner Drift Glow" — subtle, clearly different from Classic.
    //
    // Instead of a sweep, we softly bias brightness toward a corner that slowly
    // (and pseudo-randomly) changes over time, with a smooth crossfade.
    const uint32_t segMs = 7000; // how long each corner "holds" before drifting
    const uint32_t seg = (segMs ? (nowMs / segMs) : 0);
    const uint32_t segT = (segMs ? (nowMs % segMs) : 0);
    const uint8_t mix = (segMs ? (uint8_t)((segT * 255UL) / segMs) : 0);

    // Pick two corners for this segment and the next (deterministic "random")
    const uint8_t cornerA = (uint8_t)(hash8(seg * 53u + (uint32_t)level * 17u) & 3u);
    const uint8_t cornerB = (uint8_t)(hash8((seg + 1u) * 53u + (uint32_t)level * 17u) & 3u);

    auto glowForCorner = [&](uint8_t c) -> uint8_t {
      // Corners: 0=TL, 1=TR, 2=BR, 3=BL
      const bool top  = (c == 0 || c == 1);
      const bool left = (c == 0 || c == 3);

      const uint8_t dx = left ? bx : (uint8_t)(MATRIX_W - 1 - bx);
      const uint8_t dy = top  ? by : (uint8_t)(MATRIX_H - 1 - by);

      // Manhattan distance from the corner (0 at the corner)
      const uint8_t dist = (uint8_t)(dx + dy);

      // Convert to strength 0..255 with a soft falloff.
      // Max possible dist in 16x16 is 30.
      const uint8_t maxDist = (uint8_t)((MATRIX_W - 1) + (MATRIX_H - 1));
      int s = (int)maxDist - (int)dist;
      if (s < 0) s = 0;

      uint16_t strength = (uint16_t)((uint32_t)s * 255UL / (uint32_t)maxDist);

      // Ease (square) for a nicer gradient
      strength = (uint16_t)((strength * strength) >> 8);
      return (uint8_t)strength;
    };

    const uint8_t gA = glowForCorner(cornerA);
    const uint8_t gB = glowForCorner(cornerB);
    const uint8_t g  = (uint8_t)((((uint16_t)gA * (uint16_t)(255 - mix)) + ((uint16_t)gB * (uint16_t)mix)) >> 8);

    // Apply as a gentle bias: outer ring shows it most, inner ring least.
    const uint8_t amp = (ring == 0) ? 58 : (ring == 1) ? 26 : 10;
    int boost = (int)((uint16_t)g * amp / 255);

    int vv = (int)val + boost;
    if (vv > 210) vv = 210;
    val = (uint8_t)vv;

    // Tiny hue nudge so the drift reads as a "mode change" without being loud.
    if (!mutedFirstLevel && ring != 2) {
      const uint16_t hueWobble = (uint16_t)((uint32_t)g * 900UL / 255UL); // 0..~900
      hue2 = (uint16_t)(hue2 + hueWobble);
    }

    // Optional: a very small "pin" highlight right at the active corner (outer ring only).
    if (ring == 0 && g > 220) {
      int v2 = (int)val + 18;
      if (v2 > 225) v2 = 225;
      val = (uint8_t)v2;
    }
  } else if (style == 2) {
    // STYLE 2 (21-30): "Sparkle twinkle" — occasional white glints that pop.
    const uint32_t tick = (uint32_t)(nowMs / 170);
    const uint8_t h = hash8((uint32_t)level * 257u + (uint32_t)bx * 41u + (uint32_t)by * 97u + tick * 53u + (uint32_t)ring * 151u);

    // ~2–4 sparkles across the whole border at any given moment
    if (h < 6 && ring != 2) {
      sat = (uint8_t)(sat / 5); // subtle sparkle (desaturated)
      int vv = (int)val + 80;
      if (vv > 220) vv = 220;
      val = (uint8_t)vv;
    } else {
      // Gentle scintillation (keeps it lively even when not sparkling)
      const int shimmer = (int)tri8((uint8_t)(((nowMs >> 3) + (bx * 11) + (by * 7)) & 0xFF)) - 128;
      int dv2 = (shimmer * ((ring == 0) ? 10 : (ring == 1) ? 6 : 3)) / 128;

      int vv = (int)val + dv2;
      if (vv < vMin) vv = vMin;
      if (vv > vMax) vv = vMax;
      val = (uint8_t)vv;
    }
  } else if (style == 3) {
    // STYLE 3 (31-40): "Wavy pulse" — a traveling wave that shifts brightness + hue.
    const int wA = (int)tri8((uint8_t)(((nowMs >> 3) + (by * 13)) & 0xFF)) - 128;
    const int wB = (int)tri8((uint8_t)(((nowMs >> 2) + (by * 7) + (bx * 19)) & 0xFF)) - 128;

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

    // Reactive "sphere of light" around the falling piece.
  // Border pixels near the active piece get a gentle brightness lift, slight desaturation,
  // and a tiny hue nudge (so it reads as interactive, but stays subtle).
  if (g_focusActive && g_focusStrength) {
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
    }
  }

// If this is level 1, force grayscale while keeping the same brightness motion + glints.
  if (mutedFirstLevel) {
    sat = 0;
    hue2 = 0;
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

        setPixel(x, y, bc);
      }
    }
  }

  auto b = g.board();
  const bool clearing = g.isClearingLines();
  const uint32_t elapsed = clearing ? g.clearingElapsedMs(nowMs) : 0;
  const uint8_t alphaLin = g.clearingAlpha(nowMs);
  const uint8_t alpha = (uint8_t)(((uint16_t)alphaLin * (uint16_t)alphaLin) >> 8);

  // Compute a focus point from the current falling piece so borders can react to it.
  g_focusActive = false;
  g_focusStrength = 0;
  g_focusX = -1;
  g_focusY = -1;

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
    }
  }

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
  if (g_levelOverlayActive && !g.isGameOver()) {
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