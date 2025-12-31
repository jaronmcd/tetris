#include <Arduino.h>
#include "tetris.h"
#include "shapes.h"
#include <string.h>
#include "esp_system.h"
const uint16_t (*TetrisGame::getShapes())[4] { return TETRIS_SHAPES; }

bool TetrisGame::maskCell(uint16_t m, uint8_t r, uint8_t c) {
  uint8_t bit = 15 - (r * 4 + c);
  return (m >> bit) & 1;
}

bool TetrisGame::fits(const uint8_t board[BOARD_H][BOARD_W], uint8_t type, uint8_t rot, int8_t px, int8_t py) {
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

void TetrisGame::begin() {
  loadHighScore();
  reset();
}

void TetrisGame::loadHighScore() {
  prefs_.begin("tetris", true); 
  highScore_ = prefs_.getUInt("hs", 0);
  highLevel_ = (uint8_t)prefs_.getUInt("hl", 1);
  // New primary record for the tiny LED version: highest level ever reached.
  maxLevel_ = (uint8_t)prefs_.getUInt("ml", 1);
  hasPlayed_ = prefs_.getBool("pg", false);
  prefs_.end();
}

void TetrisGame::formatStorage() {
  Serial.println(">>> FORMATTING HIGH SCORE STORAGE <<<");
  prefs_.begin("tetris", false); 
  prefs_.clear();
  prefs_.end();
  highScore_ = 0;
  highLevel_ = 1;
  maxLevel_ = 1;
  hasPlayed_ = false;
}

bool TetrisGame::saveHighScore() {
  if (score_ > highScore_) {
    highScore_ = score_;
    highLevel_ = level_; 
    
    prefs_.begin("tetris", false); 
    prefs_.putUInt("hs", highScore_);
    prefs_.putUInt("hl", (uint32_t)highLevel_);
    prefs_.end();
    return true; 
  }
  return false;
}

bool TetrisGame::saveMaxLevel() {
  if (level_ > maxLevel_) {
    maxLevel_ = level_;
    prefs_.begin("tetris", false);
    prefs_.putUInt("ml", (uint32_t)maxLevel_);
    prefs_.end();
    return true;
  }
  return false;
}

bool TetrisGame::saveHasPlayed() {
  if (!hasPlayed_) {
    hasPlayed_ = true;
    prefs_.begin("tetris", false);
    prefs_.putBool("pg", true);
    prefs_.end();
    return true;
  }
  return false;
}

void TetrisGame::reset() {
  memset(board_, 0, sizeof(board_));
  score_ = 0;
  linesCleared_ = 0;
  level_ = 1;
  pieceSeq_ = 0;
  gameOver_ = false;
  lastType_ = 0; 
  clearing_ = false;
  clearCount_ = 0;
  clearStartMs_ = 0;
  clearDurationMs_ = CLEAR_DURATION_MS;
  suppressClearScoring_ = false;
  testMode_ = false;
  allowHighScore_ = true;
  bagIdx_ = 7;
  refillBag();
  spawnNext();
  lastFallMs_ = millis();
}

bool TetrisGame::isClearingRow(uint8_t y) const {
  if (!clearing_) return false;
  for (uint8_t i = 0; i < clearCount_; i++) {
    if (clearRows_[i] == y) return true;
  }
  return false;
}

uint32_t TetrisGame::clearingElapsedMs(uint32_t nowMs) const {
  if (!clearing_) return 0;
  uint32_t elapsed = (nowMs >= clearStartMs_) ? (nowMs - clearStartMs_) : 0;
  if (elapsed > clearDurationMs_) elapsed = clearDurationMs_;
  return elapsed;
}

uint8_t TetrisGame::clearingAlpha(uint32_t nowMs) const {
  if (!clearing_) return 255;
  uint32_t elapsed = (nowMs >= clearStartMs_) ? (nowMs - clearStartMs_) : 0;
  if (elapsed >= clearDurationMs_) return 0;
  uint32_t a = 255 - (elapsed * 255) / clearDurationMs_;
  return (uint8_t)a;
}

void TetrisGame::refillBag() {
  for (uint8_t i = 0; i < 7; i++) bag_[i] = i;
  for (int i = 6; i > 0; i--) {
    uint32_t r = esp_random();
    int j = (int)(r % (i + 1));
    uint8_t tmp = bag_[i];
    bag_[i] = bag_[j];
    bag_[j] = tmp;
  }
  bagIdx_ = 0;
}

uint8_t TetrisGame::nextPieceType() {
  if (bagIdx_ >= 7) refillBag();
  return bag_[bagIdx_++];
}

void TetrisGame::updateLevel() {
  level_ = (linesCleared_ / 10) + 1;
}

uint32_t TetrisGame::dropIntervalMs() const {
  int base = 700;
  int dec = (int)(level_ - 1) * 45;
  int ms = base - dec;
  if (ms < 90) ms = 90;
  return (uint32_t)ms;
}

void TetrisGame::spawnNext() {
  pieceSeq_++;
  cur_.type = nextPieceType();
  cur_.rot = 0;
  cur_.x = 3;
  cur_.y = -1;
  if (!fits(board_, cur_.type, cur_.rot, cur_.x, cur_.y)) gameOver_ = true;
}

void TetrisGame::placePieceToBoard(const Piece& p) {
  uint16_t m = TETRIS_SHAPES[p.type][p.rot & 3];
  uint8_t id = (uint8_t)(p.type + 1);
  for (uint8_t r = 0; r < 4; r++) {
    for (uint8_t c = 0; c < 4; c++) {
      if (!maskCell(m, r, c)) continue;
      int8_t bx = p.x + (int8_t)c;
      int8_t by = p.y + (int8_t)r;
      if (by < 0) continue;
      if (bx >= 0 && bx < BOARD_W && by >= 0 && by < BOARD_H) {
        board_[by][bx] = id;
      }
    }
  }
}

uint8_t TetrisGame::findFullRows(uint8_t* outRows) const {
  uint8_t count = 0;
  for (int y = BOARD_H - 1; y >= 0; y--) {
    bool full = true;
    for (uint8_t x = 0; x < BOARD_W; x++) {
      if (board_[y][x] == 0) { full = false; break; }
    }
    if (full) {
      outRows[count++] = (uint8_t)y;
      if (count >= 4) break;
    }
  }
  return count;
}

void TetrisGame::beginLineClear(uint32_t nowMs, const uint8_t* rows, uint8_t count) {
  clearing_ = true;
  clearCount_ = count;
  for (uint8_t i = 0; i < 4; i++) clearRows_[i] = 0;
  for (uint8_t i = 0; i < count && i < 4; i++) clearRows_[i] = rows[i];
  clearStartMs_ = nowMs;
  clearDurationMs_ = (count >= 4) ? (CLEAR_DURATION_MS * 2) : CLEAR_DURATION_MS;
}


void TetrisGame::debugForceLineClear(uint32_t nowMs, uint8_t lines) {
  if (lines < 1) return;
  if (lines > 4) lines = 4;

  // Don't interrupt an active clear animation.
  if (clearing_ || gameOver_) return;

  // Mark this run as "test" so highscores aren't written accidentally.
  testMode_ = true;
  allowHighScore_ = false;

  // Don't award score/lines/level for forced clears.
  suppressClearScoring_ = true;

  // Clear the bottom N rows so the effect is easy to see.
  uint8_t rows[4];
  for (uint8_t i = 0; i < lines; i++) {
    rows[i] = (uint8_t)(BOARD_H - 1 - i);
  }

  // Fill those rows with a visible pattern (IDs 1..7).
  for (uint8_t i = 0; i < lines; i++) {
    uint8_t y = rows[i];
    for (uint8_t x = 0; x < BOARD_W; x++) {
      board_[y][x] = (uint8_t)(((x + i) % 7) + 1);
    }
  }

  beginLineClear(nowMs, rows, lines);
}

void TetrisGame::debugSetLevel(uint32_t nowMs, uint8_t level) {
  if (level < 1) level = 1;
  if (level > 99) level = 99;

  // Don't interrupt an active clear animation.
  if (clearing_ || gameOver_) return;

  // Mark this run as "test" so highscores aren't written accidentally.
  testMode_ = true;

  // Keep internal accounting consistent with updateLevel().
  linesCleared_ = (uint16_t)((uint16_t)(level - 1) * 10u);
  level_ = level;

  // Reset fall timer so you don't get an immediate surprise drop.
  lastFallMs_ = nowMs;
}
void TetrisGame::applyLineClear(bool& levelUp) {
  uint8_t newBoard[BOARD_H][BOARD_W];
  memset(newBoard, 0, sizeof(newBoard));

  int dst = BOARD_H - 1;
  for (int y = BOARD_H - 1; y >= 0; y--) {
    if (isClearingRow((uint8_t)y)) continue;
    memcpy(newBoard[dst], board_[y], BOARD_W);
    dst--;
  }

  memcpy(board_, newBoard, sizeof(board_));

  uint8_t clearedThis = clearCount_;
  if (clearedThis > 0 && !suppressClearScoring_) {
    linesCleared_ += clearedThis;

    uint8_t oldLevel = level_;
    updateLevel();
    if (level_ != oldLevel) levelUp = true;

    uint32_t add = 0;
    switch (clearedThis) {
      case 1: add = 40; break;
      case 2: add = 100; break;
      case 3: add = 300; break;
      default: add = 1200; break;
    }
    score_ += add * level_;
  }
}

void TetrisGame::lockAndContinue(uint32_t nowMs, bool& levelUp) {
  lastType_ = cur_.type;
  placePieceToBoard(cur_);
  uint8_t rows[4];
  uint8_t cnt = findFullRows(rows);
  if (cnt > 0) {
    beginLineClear(nowMs, rows, cnt);
    return;
  }
  spawnNext();
  lastFallMs_ = nowMs;
}

void TetrisGame::tryMove(uint32_t nowMs, int8_t dx, int8_t dy, bool& levelUp) {
  int8_t nx = cur_.x + dx;
  int8_t ny = cur_.y + dy;
  if (fits(board_, cur_.type, cur_.rot, nx, ny)) {
    cur_.x = nx;
    cur_.y = ny;
  } else if (dy == 1) {
    lockAndContinue(nowMs, levelUp);
  }
}

void TetrisGame::tryRotate() {
  uint8_t nr = (cur_.rot + 1) & 3;
  if (fits(board_, cur_.type, nr, cur_.x, cur_.y)) { cur_.rot = nr; return; }
  if (fits(board_, cur_.type, nr, cur_.x - 1, cur_.y)) { cur_.x -= 1; cur_.rot = nr; return; }
  if (fits(board_, cur_.type, nr, cur_.x + 1, cur_.y)) { cur_.x += 1; cur_.rot = nr; return; }
}

void TetrisGame::hardDrop(uint32_t nowMs, bool& levelUp) {
  while (fits(board_, cur_.type, cur_.rot, cur_.x, cur_.y + 1)) {
    cur_.y += 1;
    score_ += 1;
  }
  lockAndContinue(nowMs, levelUp);
}

// THIS IS THE CORRECT TICK FUNCTION
TetrisGame::TickResult TetrisGame::tick(uint32_t nowMs, const Actions& a, bool allowHighScore) {
  TickResult tr;

  // Remember whether this run is allowed to write highscores. The display uses
  // this to decide when to enable "chasing the record" border FX.
  allowHighScore_ = allowHighScore && !testMode_;

  if (a.restart) {
    reset();
    return tr;
  }
  
  if (gameOver_) {
      // Persist records only when this run is allowed to.
      if (allowHighScore_) {
        (void)saveHasPlayed();
        (void)saveHighScore();
        if (saveMaxLevel()) tr.newMaxLevel = true;
      }
      tr.gameOver = true;
      return tr;
  }


  // Serial-only debug: force a line-clear effect (no scoring, disables highscores for this run)
  if (a.testClearLines > 0 && !clearing_ && !gameOver_) {
    debugForceLineClear(nowMs, (uint8_t)a.testClearLines);
    return tr;
  }

  if (clearing_) {
    if ((nowMs - clearStartMs_) >= clearDurationMs_) {
      applyLineClear(tr.levelUp);
      clearing_ = false;
      clearCount_ = 0;
      clearStartMs_ = 0;
      clearDurationMs_ = CLEAR_DURATION_MS;
      suppressClearScoring_ = false;
      spawnNext();
      lastFallMs_ = nowMs;
      tr.linesCleared = true;
    }
    return tr;
  }

  if (a.left)  tryMove(nowMs, -1, 0, tr.levelUp);
  if (a.right) tryMove(nowMs, +1, 0, tr.levelUp);
  if (a.rotate) tryRotate();

  if (a.drop) {
    hardDrop(nowMs, tr.levelUp);
    lastFallMs_ = nowMs;
    if (gameOver_) { 
        if (allowHighScore_) {
          (void)saveHasPlayed();
          (void)saveHighScore();
          if (saveMaxLevel()) tr.newMaxLevel = true;
        }
        tr.gameOver = true; 
    } 
    return tr;
  }

  if (a.down) {
    if ((nowMs - lastFallMs_) >= 60) {
      lastFallMs_ = nowMs;
      tryMove(nowMs, 0, +1, tr.levelUp);
    }
    if (gameOver_) { 
        if (allowHighScore_) {
          (void)saveHasPlayed();
          (void)saveHighScore();
          if (saveMaxLevel()) tr.newMaxLevel = true;
        }
        tr.gameOver = true; 
    } 
    return tr;
  }

  if ((nowMs - lastFallMs_) >= dropIntervalMs()) {
    lastFallMs_ = nowMs;
    tryMove(nowMs, 0, +1, tr.levelUp);
    if (gameOver_) { 
        if (allowHighScore_) {
          (void)saveHasPlayed();
          (void)saveHighScore();
          if (saveMaxLevel()) tr.newMaxLevel = true;
        }
        tr.gameOver = true; 
    } 
  }

  return tr;
}

void TetrisGame::getCurrentPieceBlocks(Cell* out, uint8_t& count) const {
  count = 0;
  uint16_t m = TETRIS_SHAPES[cur_.type][cur_.rot & 3];
  for (uint8_t r = 0; r < 4; r++) {
    for (uint8_t c = 0; c < 4; c++) {
      if (!maskCell(m, r, c)) continue;
      int8_t bx = cur_.x + (int8_t)c;
      int8_t by = cur_.y + (int8_t)r;
      if (by < 0) continue;
      if (bx < 0 || bx >= BOARD_W || by >= BOARD_H) continue;
      out[count++] = {bx, by};
      if (count >= 4) return;
    }
  }
}
