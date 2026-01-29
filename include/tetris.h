#pragma once
#include <stdint.h>
#include <Preferences.h>
#include "config.h"
#include "actions.h"

class TetrisGame {
public:
  struct TickResult {
    bool levelUp = false;
    bool linesCleared = false; 
    bool gameOver = false;
    // True when the run set a new "max level" record (persistent).
    bool newMaxLevel = false;
  };

  struct Piece {
    uint8_t type = 0;   
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
  void formatStorage();

  uint32_t pieceSeq() const { return pieceSeq_; }

  // CHANGED: Added 'allowHighScore' parameter
  TickResult tick(uint32_t nowMs, const Actions& a, bool allowHighScore);

  // Debug: force a 1/2/3/4-line clear animation for FX testing (does not award score/lines, and disables high scores for this run).
  void debugForceLineClear(uint32_t nowMs, uint8_t lines);

  // Debug: manually set the level for testing palettes/transitions. This marks the
  // run as a test so highscores won't be saved.
  void debugSetLevel(uint32_t nowMs, uint8_t level);

  // Debug helper: after a blocking screen (e.g., game over preview) re-sync the
  // internal clocks so gameplay doesn't "jump" (instant drops, etc.).
  void debugResyncTimers(uint32_t nowMs);


  bool isGameOver() const { return gameOver_; }
  bool isClearingLines() const { return clearing_; }

  uint8_t level() const { return level_; }
  uint16_t lines() const { return linesCleared_; }
  uint32_t score() const { return score_; }
  
  uint32_t highScore() const { return highScore_; }
  uint8_t highLevel() const { return highLevel_; } 

  // The best (highest) level ever reached (persistent). This is the primary
  // "record" for the tiny LED version.
  uint8_t maxLevel() const { return maxLevel_; }

  // Persistent counter: how many completed (record-eligible) game-overs have
  // occurred since the current maxLevel_ record was set.
  //
  // This is used to drive a "record chase" background/progress indicator on
  // the MAX level screen.
  uint16_t maxLevelChaseAttempts() const { return maxLevelChaseAttempts_; }

  // True after at least one completed (non-test) game has occurred.
  // Used to avoid "record chase" border FX during the very first run.
  bool hasPlayedBefore() const { return hasPlayed_; }

  // True when the *current* run is allowed to write highscores (e.g., human play
  // or AI allowed by config). Display can use this for "chasing high score" FX.
  bool allowHighScore() const { return allowHighScore_; }

  const uint8_t (*board() const)[BOARD_W] { return board_; }
  Piece currentPiece() const { return cur_; }
  uint8_t currentPieceId() const { return (uint8_t)(cur_.type + 1); } 

  uint8_t lastLockedPieceType() const { return lastType_; }

  uint8_t clearingLineCount() const { return clearCount_; }
  const uint8_t* clearingLines() const { return clearRows_; }
  bool isClearingRow(uint8_t y) const;
  uint8_t clearingAlpha(uint32_t nowMs) const;            
  uint32_t clearingElapsedMs(uint32_t nowMs) const;       
  uint32_t clearDurationMs() const { return clearDurationMs_; }

  void getCurrentPieceBlocks(Cell* out, uint8_t& count) const;

  // AI getters
  int8_t currentX() const { return cur_.x; }
  int8_t currentY() const { return cur_.y; }
  uint8_t currentRotation() const { return cur_.rot; }

  // Peek the next piece in the current 7-bag (for AI lookahead).
  // Returns 0..6. If the bag is about to be refilled (next piece unknown), returns 255.
  uint8_t peekNextPieceType() const;

  static const uint16_t (*getShapes())[4]; 

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

  void loadHighScore();
  bool saveHighScore();
  bool saveMaxLevel();
  bool saveHasPlayed();
  bool saveMaxLevelChaseAttempts();

private:
  uint8_t board_[BOARD_H][BOARD_W] = {};
  Piece cur_;
  bool gameOver_ = false;

  uint8_t lastType_ = 0; 

  bool clearing_ = false;
  uint8_t clearRows_[4] = {0, 0, 0, 0};
  uint8_t clearCount_ = 0;
  static constexpr uint32_t CLEAR_DURATION_MS = 260;
  uint32_t clearStartMs_ = 0;
  uint32_t clearDurationMs_ = CLEAR_DURATION_MS;
  bool suppressClearScoring_ = false;
  bool testMode_ = false;

  uint32_t score_ = 0;
  uint32_t highScore_ = 0; 
  uint8_t highLevel_ = 1; 

  uint8_t maxLevel_ = 1;

  // How many record-eligible runs have ended without setting a new maxLevel_.
  // Resets to 0 whenever a new max level is saved.
  uint16_t maxLevelChaseAttempts_ = 0;

  bool hasPlayed_ = false;

  bool allowHighScore_ = true;
  
  uint16_t linesCleared_ = 0;
  uint8_t level_ = 1;

  uint32_t lastFallMs_ = 0;

  uint8_t bag_[7] = {};
  uint8_t bagIdx_ = 7;

  uint32_t pieceSeq_ = 0;
  
  Preferences prefs_;
};
