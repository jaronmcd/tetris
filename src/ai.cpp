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

  int bestScore = -2147483647;

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

      int heights[BOARD_W];
      int aggHeight, holes, bump;
      columnStats(tmp, heights, aggHeight, holes, bump);

      int maxH = 0;
      for (int i = 0; i < BOARD_W; i++) if (heights[i] > maxH) maxH = heights[i];

      // Heuristic:
      // + clears (big reward)
      // - holes (big penalty)
      // - height / bump / top-out risk
      int score =
          cleared * 100000
        - holes * 6000
        - aggHeight * 250
        - bump * 350
        - maxH * 200;

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
