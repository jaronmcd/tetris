#pragma once
#include <stdint.h>
#include "config.h"
#include "actions.h"

class TetrisGame {
public:
  struct TickResult {
    bool levelUp = false;
    bool linesCleared = false; // true when animation finishes and rows are removed
  };

  struct Piece {
    uint8_t type = 0;   // 0..6
    int8_t x = 3;
    int8_t y = -1;
    uint8_t rot = 0;
  };

  struct Cell {
    int8_t x;
    int8_t y;
  };

  void begin();
  void reset();

  uint32_t pieceSeq() const { return pieceSeq_; }

  TickResult tick(uint32_t nowMs, const Actions& a);

  bool isGameOver() const { return gameOver_; }
  bool isClearingLines() const { return clearing_; }

  uint8_t level() const { return level_; }
  uint16_t lines() const { return linesCleared_; }
  uint32_t score() const { return score_; }

  const uint8_t (*board() const)[BOARD_W] { return board_; }
  Piece currentPiece() const { return cur_; }
  uint8_t currentPieceId() const { return (uint8_t)(cur_.type + 1); } // 1..7

  // Line-clear animation helpers
  uint8_t clearingLineCount() const { return clearCount_; }
  const uint8_t* clearingLines() const { return clearRows_; }
  bool isClearingRow(uint8_t y) const;
  uint8_t clearingAlpha(uint32_t nowMs) const;            // 255 -> 0
  uint32_t clearingElapsedMs(uint32_t nowMs) const;       // 0..duration
  uint32_t clearDurationMs() const { return CLEAR_DURATION_MS; }

  void getCurrentPieceBlocks(Cell* out, uint8_t& count) const;

private:
  static bool maskCell(uint16_t m, uint8_t r, uint8_t c);
  static bool fits(const uint8_t board[BOARD_H][BOARD_W], uint8_t type, uint8_t rot, int8_t px, int8_t py);

  void refillBag();
  uint8_t nextPieceType();

  void spawnNext();
  void placePieceToBoard(const Piece& p);
  uint8_t findFullRows(uint8_t* outRows) const;

  void beginLineClear(uint32_t nowMs, const uint8_t* rows, uint8_t count);
  void applyLineClear(bool& levelUp);
  void updateLevel();
  uint32_t dropIntervalMs() const;

  void tryMove(uint32_t nowMs, int8_t dx, int8_t dy, bool& levelUp);
  void tryRotate();
  void hardDrop(uint32_t nowMs, bool& levelUp);
  void lockAndContinue(uint32_t nowMs, bool& levelUp);

private:
  uint8_t board_[BOARD_H][BOARD_W] = {};
  Piece cur_;
  bool gameOver_ = false;

  // Line clear fade animation state
  bool clearing_ = false;
  uint8_t clearRows_[4] = {0, 0, 0, 0};
  uint8_t clearCount_ = 0;
  uint32_t clearStartMs_ = 0;
  static constexpr uint32_t CLEAR_DURATION_MS = 260;

  uint32_t score_ = 0;
  uint16_t linesCleared_ = 0;
  uint8_t level_ = 1;

  uint32_t lastFallMs_ = 0;

  uint8_t bag_[7] = {};
  uint8_t bagIdx_ = 7;

  uint32_t pieceSeq_ = 0;
};
