#include "ai.h"
#include <string.h>
#include <Arduino.h>
#include "config.h"
#include "shapes.h"
#include "esp_system.h"

static inline bool maskCell(uint16_t m, uint8_t r, uint8_t c) {
  uint8_t bit = 15 - (r * 4 + c);
  return (m >> bit) & 1;
}

static bool fits(const uint8_t board[BOARD_H][BOARD_W], uint8_t type, uint8_t rot, int8_t px, int8_t py) {
  uint16_t m = TETRIS_SHAPES[type][rot & 3];
  for (uint8_t r = 0; r < 4; r++) {
    for (uint8_t c = 0; c < 4; c++) {
      if (!maskCell(m, r, c)) continue;

      int8_t bx = px + (int8_t)c;
      int8_t by = py + (int8_t)r;

      if (bx < 0 || bx >= BOARD_W) return false;
      if (by >= BOARD_H) return false;
      if (by < 0) continue;
      if (board[by][bx] != 0) return false;
    }
  }
  return true;
}

static void place(uint8_t board[BOARD_H][BOARD_W], uint8_t type, uint8_t rot, int8_t px, int8_t py) {
  uint16_t m = TETRIS_SHAPES[type][rot & 3];
  for (uint8_t r = 0; r < 4; r++) {
    for (uint8_t c = 0; c < 4; c++) {
      if (!maskCell(m, r, c)) continue;
      int8_t bx = px + (int8_t)c;
      int8_t by = py + (int8_t)r;
      if (by < 0) continue;
      if (bx >= 0 && bx < BOARD_W && by >= 0 && by < BOARD_H) {
        board[by][bx] = 1;
      }
    }
  }
}

static int clearLinesSim(uint8_t board[BOARD_H][BOARD_W]) {
  int cleared = 0;
  for (int y = BOARD_H - 1; y >= 0; y--) {
    bool full = true;
    for (uint8_t x = 0; x < BOARD_W; x++) {
      if (board[y][x] == 0) { full = false; break; }
    }
    if (full) {
      cleared++;
      for (int yy = y; yy > 0; yy--) {
        memcpy(board[yy], board[yy - 1], BOARD_W);
      }
      memset(board[0], 0, BOARD_W);
      y++;
    }
  }
  return cleared;
}

static void columnStats(const uint8_t board[BOARD_H][BOARD_W], int heights[BOARD_W], int& aggHeight, int& holes, int& bump) {
  aggHeight = 0;
  holes = 0;

  for (int x = 0; x < BOARD_W; x++) {
    int yFirst = -1;
    for (int y = 0; y < BOARD_H; y++) {
      if (board[y][x] != 0) { yFirst = y; break; }
    }
    if (yFirst == -1) {
      heights[x] = 0;
      continue;
    }
    heights[x] = BOARD_H - yFirst;
    aggHeight += heights[x];

    for (int y = yFirst; y < BOARD_H; y++) {
      if (board[y][x] == 0) holes++;
    }
  }

  bump = 0;
  for (int x = 0; x < BOARD_W - 1; x++) {
    bump += abs(heights[x] - heights[x + 1]);
  }
}

static void shapeBoundsCols(uint8_t type, uint8_t rot, int& minC, int& maxC) {
  uint16_t m = TETRIS_SHAPES[type][rot & 3];
  minC = 3;
  maxC = 0;
  bool any = false;
  for (uint8_t r = 0; r < 4; r++) {
    for (uint8_t c = 0; c < 4; c++) {
      if (!maskCell(m, r, c)) continue;
      any = true;
      if (c < minC) minC = c;
      if (c > maxC) maxC = c;
    }
  }
  if (!any) { minC = 0; maxC = 0; }
}



// --- AI board features / scoring (used by the MAX-chase "smartness" ladder) ---
static int rowTransitionsAI(const uint8_t board[BOARD_H][BOARD_W]) {
  int t = 0;
  for (int y = 0; y < BOARD_H; y++) {
    int prev = 1; // left wall
    for (int x = 0; x < BOARD_W; x++) {
      int cur = (board[y][x] != 0) ? 1 : 0;
      if (cur != prev) t++;
      prev = cur;
    }
    if (prev == 0) t++; // right wall
  }
  return t;
}

static int colTransitionsAI(const uint8_t board[BOARD_H][BOARD_W]) {
  int t = 0;
  for (int x = 0; x < BOARD_W; x++) {
    int prev = 1; // top wall
    for (int y = 0; y < BOARD_H; y++) {
      int cur = (board[y][x] != 0) ? 1 : 0;
      if (cur != prev) t++;
      prev = cur;
    }
    if (prev == 0) t++; // bottom wall
  }
  return t;
}

static int wellSumsAI(const uint8_t board[BOARD_H][BOARD_W]) {
  // Simple "well" measure: consecutive empty cells that are bounded on both sides.
  // Encourages a clean single well and discourages random pits.
  int sum = 0;
  for (int x = 0; x < BOARD_W; x++) {
    for (int y = 0; y < BOARD_H; y++) {
      if (board[y][x] != 0) continue;
      bool leftFilled  = (x == 0) ? true : (board[y][x - 1] != 0);
      bool rightFilled = (x == (BOARD_W - 1)) ? true : (board[y][x + 1] != 0);
      if (!(leftFilled && rightFilled)) continue;

      // Depth downward in this well (bounded on both sides).
      int depth = 1;
      for (int yy = y + 1; yy < BOARD_H; yy++) {
        if (board[yy][x] != 0) break;
        bool lf = (x == 0) ? true : (board[yy][x - 1] != 0);
        bool rf = (x == (BOARD_W - 1)) ? true : (board[yy][x + 1] != 0);
        if (!(lf && rf)) break;
        depth++;
      }
      // Triangular sum (1+2+...+depth) favors deeper wells.
      sum += (depth * (depth + 1)) / 2;
    }
  }
  return sum;
}

struct AIWeights {
  int wClear;
  int wHoles;
  int wAggH;
  int wBump;
  int wMaxH;
  int wRowT;
  int wColT;
  int wWells;
  int wLand;
};

static inline uint8_t weightTierForSkill(uint8_t skill) {
  // Skill 3 is "skill 2 weights + lookahead".
  if (skill == 3) return 2;
  if (skill >= 4) return 4;
  return skill;
}

static AIWeights weightsForTier(uint8_t tier) {
  // Base weights (tier=1): current tuned values.
  AIWeights w{};
  w.wClear = 100000;
  w.wHoles = 6000;
  w.wAggH  = 250;
  w.wBump  = 350;
  w.wMaxH  = 200;
  w.wRowT  = 0;
  w.wColT  = 0;
  w.wWells = 0;
  w.wLand  = 0;

  if (tier >= 2) {
    w.wHoles = 6500;
    w.wAggH  = 270;
    w.wBump  = 380;
    w.wMaxH  = 230;
    w.wRowT  = 120;
    w.wColT  = 160;
    w.wWells = 60;
    w.wLand  = 55;
  }

  if (tier >= 4) {
    w.wHoles = 7200;
    w.wAggH  = 300;
    w.wBump  = 420;
    w.wMaxH  = 280;
    w.wRowT  = 140;
    w.wColT  = 190;
    w.wWells = 85;
    w.wLand  = 70;
  }

  if (tier == 0) {
    // Intentionally a little "sloppy" for very low skill (useful for testing the ladder).
    w.wHoles = 4200;
    w.wAggH  = 200;
    w.wBump  = 260;
    w.wMaxH  = 160;
    w.wRowT  = 0;
    w.wColT  = 0;
    w.wWells = 0;
    w.wLand  = 0;
  }

  return w;
}

static inline int lerpInt(int a, int b, uint8_t pct) {
  // pct: 0..100
  return a + ((b - a) * (int)pct) / 100;
}

static AIWeights blendWeights(const AIWeights& a, const AIWeights& b, uint8_t pct) {
  AIWeights o;
  o.wClear = lerpInt(a.wClear, b.wClear, pct);
  o.wHoles = lerpInt(a.wHoles, b.wHoles, pct);
  o.wAggH  = lerpInt(a.wAggH,  b.wAggH,  pct);
  o.wBump  = lerpInt(a.wBump,  b.wBump,  pct);
  o.wMaxH  = lerpInt(a.wMaxH,  b.wMaxH,  pct);
  o.wRowT  = lerpInt(a.wRowT,  b.wRowT,  pct);
  o.wColT  = lerpInt(a.wColT,  b.wColT,  pct);
  o.wWells = lerpInt(a.wWells, b.wWells, pct);
  o.wLand  = lerpInt(a.wLand,  b.wLand,  pct);
  return o;
}

static int evalPlacementScoreAI(const uint8_t board[BOARD_H][BOARD_W], int cleared, int landingY, uint8_t skillBase, uint8_t rampPct) {
  int heights[BOARD_W];
  int aggHeight = 0, holes = 0, bump = 0;
  columnStats(board, heights, aggHeight, holes, bump);

  int maxH = 0;
  for (int i = 0; i < BOARD_W; i++) if (heights[i] > maxH) maxH = heights[i];

  // landing height: higher placement is riskier (top-out). Clamp y.
  if (landingY < 0) landingY = 0;
  if (landingY > (BOARD_H - 1)) landingY = (BOARD_H - 1);
  int landingHeight = BOARD_H - landingY;

  uint8_t s0 = skillBase;
  if (s0 > (uint8_t)AI_SMARTNESS_MAX) s0 = (uint8_t)AI_SMARTNESS_MAX;

  // Blend weights toward the next skill tier as the MAX-chase bar fills.
  // This keeps the *major* milestones identical (skill changes only on full cycles),
  // but makes the AI a little better with each ...
  uint8_t s1 = s0;
  uint8_t pct = 0;
  if (s0 < (uint8_t)AI_SMARTNESS_MAX) {
    s1 = (uint8_t)(s0 + 1);
    pct = rampPct;
    if (pct > 100) pct = 100;
  }

  uint8_t t0 = weightTierForSkill(s0);
  uint8_t t1 = weightTierForSkill(s1);
  AIWeights w = (t0 == t1 || pct == 0) ? weightsForTier(t0)
                                      : blendWeights(weightsForTier(t0), weightsForTier(t1), pct);

  int rt = (w.wRowT != 0) ? rowTransitionsAI(board) : 0;
  int ct = (w.wColT != 0) ? colTransitionsAI(board) : 0;
  int wells = (w.wWells != 0) ? wellSumsAI(board) : 0;

  int score = 0;
  score += cleared * w.wClear;
  score -= holes * w.wHoles;
  score -= aggHeight * w.wAggH;
  score -= bump * w.wBump;
  score -= maxH * w.wMaxH;
  score -= rt * w.wRowT;
  score -= ct * w.wColT;
  score -= wells * w.wWells;
  score -= landingHeight * w.wLand;

  if (skillBase == 0) {
    // Add a little randomness so it visibly "learns" as skill rises.
    int32_t n = (int32_t)(esp_random() % 20001u) - 10000; // [-10k, +10k]
    score += (int)n;
  }

  return score;
}

static int bestScoreForPieceAI(const uint8_t base[BOARD_H][BOARD_W], uint8_t type, uint8_t skill) {
  const int kNegInf = -2147483647;
  int best = kNegInf;

  for (uint8_t rot = 0; rot < 4; rot++) {
    int minC, maxC;
    shapeBoundsCols(type, rot, minC, maxC);

    int8_t minX = (int8_t)(-minC);
    int8_t maxX = (int8_t)(BOARD_W - 1 - maxC);

    for (int8_t x = minX; x <= maxX; x++) {
      int8_t y = -4;
      if (!fits(base, type, rot, x, y)) continue;

      while (fits(base, type, rot, x, (int8_t)(y + 1))) y++;

      uint8_t tmp[BOARD_H][BOARD_W];
      memcpy(tmp, base, sizeof(tmp));
      place(tmp, type, rot, x, y);
      int cleared = clearLinesSim(tmp);

      int score = evalPlacementScoreAI(tmp, cleared, y, skill, 0);
      if (score > best) best = score;
    }
  }

  return best;
}
uint32_t TetrisAI::jitterMs(uint32_t base, uint32_t spread) const {
  // base +/- up to spread ms (clamped at 0)
  uint32_t r = esp_random();
  int32_t j = (int32_t)(r % (spread * 2 + 1)) - (int32_t)spread;
  int32_t v = (int32_t)base + j;
  if (v < 0) v = 0;
  return (uint32_t)v;
}

uint32_t TetrisAI::scaleMs(uint32_t v) const {
  // Multipliers per profile:
  // 0 slow:  1.6x
  // 1 normal:1.0x
  // 2 fast:  0.7x
  // 3 turbo: 0.35x
  static const uint16_t num[4] = { 16, 10, 7, 35 };
  static const uint16_t den[4] = { 10, 10, 10, 100 };
  uint8_t p = profile_;
  if (p > 3) p = 3;
  uint32_t out = (v * (uint32_t)num[p]) / (uint32_t)den[p];
  if (out < 5) out = 5;
  return out;
}

void TetrisAI::setProfile(uint8_t p) {
  if (p > 3) p = 3;
  profile_ = p;
  reset();
}



void TetrisAI::setSkill(uint8_t s) {
  // Clamp to configured range.
  if (s > (uint8_t)AI_SMARTNESS_MAX) s = (uint8_t)AI_SMARTNESS_MAX;
  if (skill_ == s) return;
  skill_ = s;
  reset();
}

void TetrisAI::setSkillRampPct(uint8_t pct) {
  if (pct > 100) pct = 100;
  // If we're already at max skill, ramp has no effect.
  if (skill_ >= (uint8_t)AI_SMARTNESS_MAX) pct = 0;
  if (skillRampPct_ == pct) return;
  skillRampPct_ = pct;
  // Intentionally do NOT reset() here; we allow the ramp to change smoothly.
}
void TetrisAI::reset() {
  lastPieceSeq_ = 0;
  phase_ = WAIT_THINK;
  phaseUntilMs_ = 0;
  nextStepMs_ = 0;

  downPulseOn_ = true;
  nextDownToggleMs_ = 0;

  plan_ = {};
}


TetrisAI::Plan TetrisAI::computePlan(const TetrisGame& g) const {
  Plan best;
  best.valid = false;

  uint8_t base[BOARD_H][BOARD_W];
  memcpy(base, g.board(), sizeof(base));

  auto p = g.currentPiece();
  uint8_t type = p.type;

  // Skill ladder (0..AI_SMARTNESS_MAX). Higher => better placement quality.
  uint8_t skill = skill_;
  if (skill > (uint8_t)AI_SMARTNESS_MAX) skill = (uint8_t)AI_SMARTNESS_MAX;

  // 0..100 progress toward the next skill tier (driven by MAX chase bar fill).
  uint8_t rampPct = skillRampPct_;
  if (skill >= (uint8_t)AI_SMARTNESS_MAX) rampPct = 0;

  // Lookahead is the "big" milestone at skill>=3.
  // To make progress incremental per MAX-chase bar, we smoothly ramp the
  // lookahead influence as we approach/advance toward that milestone.
  uint8_t nextType = 255;
  uint8_t lookaheadWeightPct = 0;
  if (skill >= 4) {
    lookaheadWeightPct = 70;
    nextType = g.peekNextPieceType();
  } else if (skill == 3) {
    // Ramp lookahead strength toward skill-4 behavior.
    lookaheadWeightPct = (uint8_t)(55 + ((70 - 55) * (uint16_t)rampPct) / 100u);
    nextType = g.peekNextPieceType();
  } else if (skill == 2) {
    // Start introducing lookahead before the milestone (up to the normal 55%).
    lookaheadWeightPct = (uint8_t)((55u * (uint16_t)rampPct) / 100u);
    if (lookaheadWeightPct > 0) nextType = g.peekNextPieceType();
  }

  const int kNegInf = -2147483647;
  int bestScore = kNegInf;

  for (uint8_t rot = 0; rot < 4; rot++) {
    int minC, maxC;
    shapeBoundsCols(type, rot, minC, maxC);

    int8_t minX = (int8_t)(-minC);
    int8_t maxX = (int8_t)(BOARD_W - 1 - maxC);

    for (int8_t x = minX; x <= maxX; x++) {
      int8_t y = -4;
      if (!fits(base, type, rot, x, y)) continue;

      while (fits(base, type, rot, x, (int8_t)(y + 1))) y++;

      uint8_t tmp[BOARD_H][BOARD_W];
      memcpy(tmp, base, sizeof(tmp));
      place(tmp, type, rot, x, y);

      int cleared = clearLinesSim(tmp);

      int score = evalPlacementScoreAI(tmp, cleared, y, skill, rampPct);

      if (lookaheadWeightPct > 0 && nextType <= 6) {
        // Lookahead one move: choose the best placement for the next piece on this resulting board.
        uint8_t nextSkill = (skill > 0) ? (uint8_t)(skill - 1) : 0;
        int nextScore = bestScoreForPieceAI(tmp, nextType, nextSkill);
        if (nextScore != kNegInf) {
          score += (nextScore * (int)lookaheadWeightPct) / 100;
        }
      }

      if (!best.valid || score > bestScore) {
        best.valid = true;
        bestScore = score;
        best.rot = rot;
        best.x = x;
      }
    }
  }

  if (!best.valid) {
    best.valid = true;
    best.rot = 0;
    best.x = 3;
  }
  return best;
}
Actions TetrisAI::think(const TetrisGame& g, uint32_t nowMs) {
  Actions a;

  // Demo mode: auto-restart if it tops out
  if (g.isGameOver()) {
    a.restart = true;
    return a;
  }

  // Re-plan on each new spawned piece
  uint32_t seq = g.pieceSeq();
  if (seq != lastPieceSeq_) {
    lastPieceSeq_ = seq;
    plan_ = computePlan(g);

    // Human reaction time before acting (varies a bit)
    phase_ = WAIT_THINK;
    phaseUntilMs_ = nowMs + scaleMs(jitterMs(220, 180)); // ~40..400ms (scaled)

    // Reset pacing + soft-drop pulsing
    nextStepMs_ = 0;
    downPulseOn_ = true;
    nextDownToggleMs_ = nowMs + scaleMs(jitterMs(520, 180)); // first toggle in ~340..700ms (scaled)
  }

  if (!plan_.valid) return a;

  // Wait / "think" phase
  if (phase_ == WAIT_THINK) {
    if (nowMs < phaseUntilMs_) return a;
    phase_ = ROTATE_TO_TARGET;
    nextStepMs_ = nowMs; // act immediately after think
  }

  // Rate-limit actions so it looks like button presses
  if (nowMs < nextStepMs_) return a;

  const uint8_t lvl = g.level();

  // Pacing: higher levels press a bit faster, still human-ish
  uint32_t rotateDelayBase = (lvl < 6) ? 140 : (lvl < 12 ? 120 : 105);
  uint32_t moveDelayBase   = (lvl < 6) ? 120 : (lvl < 12 ? 105 : 90);

  auto p = g.currentPiece();

  // Rotate toward target
  if (phase_ == ROTATE_TO_TARGET) {
    if ((p.rot & 3) != (plan_.rot & 3)) {
      a.rotate = true;
      nextStepMs_ = nowMs + scaleMs(jitterMs(rotateDelayBase, 25));
      return a;
    }
    phase_ = MOVE_TO_TARGET;
    nextStepMs_ = nowMs + scaleMs(jitterMs(60, 30));
    return a;
  }

  // Move toward target x
  if (phase_ == MOVE_TO_TARGET) {
    if (p.x < plan_.x) {
      a.right = true;
      nextStepMs_ = nowMs + scaleMs(jitterMs(moveDelayBase, 25));
      return a;
    }
    if (p.x > plan_.x) {
      a.left = true;
      nextStepMs_ = nowMs + scaleMs(jitterMs(moveDelayBase, 25));
      return a;
    }

    // Aligned: start soft dropping (no hard drop)
    phase_ = SOFT_DROP;
    nextStepMs_ = nowMs + scaleMs(jitterMs(40, 30));
    return a;
  }

  // Soft drop phase: hold down in pulses so it looks human (not a perfect constant hold)
  if (phase_ == SOFT_DROP) {
    // Turbo profile is for quickly testing level progression (not "human")
    if (profile_ == 3) {
      a.down = true;
      // occasional hard drop to speed up tests
      if ((esp_random() % 100) < 35) a.drop = true;
      nextStepMs_ = nowMs + scaleMs(jitterMs(45, 15));
      return a;
    }

    if (nowMs >= nextDownToggleMs_) {
      downPulseOn_ = !downPulseOn_;
      // On a bit longer than off
      nextDownToggleMs_ = nowMs + scaleMs(downPulseOn_ ? jitterMs(520, 180) : jitterMs(160, 80));
    }

    // Occasionally “hesitate” (no input) even while dropping (very small chance)
    if ((esp_random() % 100) < 3) {
      nextStepMs_ = nowMs + scaleMs(jitterMs(120, 60));
      return a;
    }

    a.down = downPulseOn_;

    // Occasionally nudge if we drifted from target (rare)
    int8_t dx = plan_.x - p.x;
    if (dx != 0 && (esp_random() % 100) < 25) {
      if (dx > 0) a.right = true;
      else a.left = true;
      nextStepMs_ = nowMs + scaleMs(jitterMs(moveDelayBase, 25));
      return a;
    }

    // Keep calling frequently so down pulses feel responsive
    nextStepMs_ = nowMs + scaleMs(jitterMs(45, 20));
    return a;
  }

  return a;
}

void BreakoutAI::setProfile(uint8_t p) {
  if (p > 3) p = 3;
  profile_ = p;
  reset();
}

void BreakoutAI::reset() {
  nextStepMs_ = 0;
  launchAtMs_ = 0;
  gameOverRestartMs_ = 0;
}

uint32_t BreakoutAI::jitterMs(uint32_t base, uint32_t spread) const {
  uint32_t r = esp_random();
  int32_t j = (int32_t)(r % (spread * 2 + 1u)) - (int32_t)spread;
  int32_t v = (int32_t)base + j;
  if (v < 0) v = 0;
  return (uint32_t)v;
}

uint32_t BreakoutAI::scaleMs(uint32_t v) const {
  // 0 slow, 1 normal, 2 fast, 3 turbo
  static const uint16_t num[4] = {16, 10, 7, 35};
  static const uint16_t den[4] = {10, 10, 10, 100};
  uint8_t p = profile_;
  if (p > 3) p = 3;
  uint32_t out = (v * (uint32_t)num[p]) / (uint32_t)den[p];
  if (out < 5) out = 5;
  return out;
}

uint8_t BreakoutAI::predictTargetX(const BreakoutGame& g) const {
  int16_t x = g.ballXQ8();
  int16_t y = g.ballYQ8();
  int16_t vx = g.ballVelXQ8();
  int16_t vy = g.ballVelYQ8();

  if (vx == 0 && vy == 0) return g.ballX();
  if (vy == 0) vy = 96;

  const int16_t minXQ8 = 0;
  const int16_t maxXQ8 = (int16_t)((MATRIX_W - 1) << 8);
  const int16_t minYQ8 = 0;
  const int16_t paddleInterceptYQ8 = (int16_t)((BreakoutGame::PADDLE_Y - 1) << 8);

  for (uint16_t i = 0; i < 320; i++) {
    x = (int16_t)(x + vx);
    y = (int16_t)(y + vy);

    if (x < minXQ8) {
      x = minXQ8;
      vx = (int16_t)-vx;
    } else if (x > maxXQ8) {
      x = maxXQ8;
      vx = (int16_t)-vx;
    }

    if (y < minYQ8) {
      y = minYQ8;
      vy = (int16_t)-vy;
    }

    if (vy > 0 && y >= paddleInterceptYQ8) break;
  }

  int16_t px = (int16_t)((x + 128) >> 8);
  if (px < 0) px = 0;
  if (px > (MATRIX_W - 1)) px = (MATRIX_W - 1);
  return (uint8_t)px;
}

Actions BreakoutAI::think(const BreakoutGame& g, uint32_t nowMs) {
  Actions a;

  if (g.isGameOver()) {
    if (gameOverRestartMs_ == 0) {
      gameOverRestartMs_ = nowMs + scaleMs(jitterMs(700, 300));
    }
    if (nowMs >= gameOverRestartMs_) {
      a.drop = true;
      gameOverRestartMs_ = nowMs + scaleMs(jitterMs(800, 250));
    }
    return a;
  }
  gameOverRestartMs_ = 0;

  const int16_t halfW = (int16_t)((g.paddleWidth() - 1) / 2);
  const int16_t maxLeft = (int16_t)(MATRIX_W - g.paddleWidth());

  if (g.waitingLaunch()) {
    // Stage launches from roughly center so openings are safer.
    int16_t desiredLeft = (int16_t)(MATRIX_W / 2) - halfW;
    if (desiredLeft < 0) desiredLeft = 0;
    if (desiredLeft > maxLeft) desiredLeft = maxLeft;

    if (nowMs >= nextStepMs_) {
      if (g.paddleX() < desiredLeft) {
        a.right = true;
        nextStepMs_ = nowMs + scaleMs(jitterMs(95, 30));
        return a;
      }
      if (g.paddleX() > desiredLeft) {
        a.left = true;
        nextStepMs_ = nowMs + scaleMs(jitterMs(95, 30));
        return a;
      }
    }

    if (launchAtMs_ == 0) {
      launchAtMs_ = nowMs + scaleMs(jitterMs(480, 180));
    }
    if (nowMs >= launchAtMs_) {
      a.rotate = true;
      launchAtMs_ = 0;
      nextStepMs_ = nowMs + scaleMs(jitterMs(130, 40));
    }
    return a;
  }
  launchAtMs_ = 0;

  if (nowMs < nextStepMs_) return a;

  uint8_t targetX = predictTargetX(g);
  int16_t desiredLeft = (int16_t)targetX - halfW;
  if (desiredLeft < 0) desiredLeft = 0;
  if (desiredLeft > maxLeft) desiredLeft = maxLeft;

  if ((int16_t)g.paddleX() < desiredLeft) {
    a.right = true;
    nextStepMs_ = nowMs + scaleMs(jitterMs(85, 25));
    return a;
  }
  if ((int16_t)g.paddleX() > desiredLeft) {
    a.left = true;
    nextStepMs_ = nowMs + scaleMs(jitterMs(85, 25));
    return a;
  }

  nextStepMs_ = nowMs + scaleMs(jitterMs(60, 20));
  return a;
}
