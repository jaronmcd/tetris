#pragma once
#include <stdint.h>
#include "config.h"
#include "actions.h"

class TetrisGame {
public:
  struct TickResult {
    bool levelUp = false;
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

  // Update game using Actions; returns if level changed this tick.
  TickResult tick(uint32_t nowMs, const Actions& a);

  // Read-only state for rendering/UI
  bool isGameOver() const { return gameOver_; }
  uint8_t level() const { return level_; }
  uint16_t lines() const { return linesCleared_; }
  uint32_t score() const { return score_; }

  const uint8_t (*board() const)[BOARD_W] { return board_; }
  Piece currentPiece() const { return cur_; }
  uint8_t currentPieceId() const { return (uint8_t)(cur_.type + 1); } // 1..7

  // Get blocks of current piece in board coordinates (0..BOARD_W-1, 0..BOARD_H-1), y>=0 only.
  void getCurrentPieceBlocks(Cell* out, uint8_t& count) const;

private:
  static bool maskCell(uint16_t m, uint8_t r, uint8_t c);
  static bool fits(const uint8_t board[BOARD_H][BOARD_W], uint8_t type, uint8_t rot, int8_t px, int8_t py);

  void refillBag();
  uint8_t nextPieceType();

  void spawnNext();
  void placePieceToBoard(const Piece& p);
  void clearLines(bool& levelUp);
  void updateLevel();
  uint32_t dropIntervalMs() const;
  uint32_t pieceSeq_ = 0;

  void tryMove(int8_t dx, int8_t dy, bool& levelUp);
  void tryRotate();
  void hardDrop(bool& levelUp);
  void lockAndContinue(bool& levelUp);

private:
  uint8_t board_[BOARD_H][BOARD_W] = {};
  Piece cur_;
  bool gameOver_ = false;

  uint32_t score_ = 0;
  uint16_t linesCleared_ = 0;
  uint8_t level_ = 1;

  uint32_t lastFallMs_ = 0;

  uint8_t bag_[7] = {};
  uint8_t bagIdx_ = 7;
};
