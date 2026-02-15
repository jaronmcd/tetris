#pragma once

#include <stdint.h>
#include "actions.h"
#include "config.h"

class BreakoutGame {
public:
  static constexpr uint8_t BRICK_ROWS = 5;
  static constexpr uint8_t BRICK_COLS = 8;
  static constexpr uint8_t BRICK_W = MATRIX_W / BRICK_COLS;
  static constexpr uint8_t PADDLE_W = 4;
  static constexpr uint8_t PADDLE_Y = MATRIX_H - 1;

  static_assert((MATRIX_W % BRICK_COLS) == 0, "MATRIX_W must be divisible by BRICK_COLS");
  static_assert(PADDLE_W < MATRIX_W, "PADDLE_W must fit inside matrix width");

  struct TickResult {
    bool lostLife = false;
    bool gameOver = false;
    bool won = false;
    bool brickBroken = false;
  };

  void begin(uint32_t nowMs);
  void reset(uint32_t nowMs);
  TickResult tick(uint32_t nowMs, const Actions& a);

  bool brickAt(uint8_t row, uint8_t col) const;
  uint8_t remainingBricks() const { return remainingBricks_; }

  uint8_t paddleX() const { return paddleX_; }
  uint8_t paddleWidth() const { return PADDLE_W; }

  uint8_t ballX() const;
  uint8_t ballY() const;
  int16_t ballXQ8() const { return ballXQ8_; }
  int16_t ballYQ8() const { return ballYQ8_; }
  int16_t ballVelXQ8() const { return velXQ8_; }
  int16_t ballVelYQ8() const { return velYQ8_; }

  bool waitingLaunch() const { return waitingLaunch_; }
  bool isGameOver() const { return gameOver_; }
  bool won() const { return won_; }

  uint8_t lives() const { return lives_; }
  uint16_t score() const { return score_; }

private:
  TickResult stepFrame();
  void resetBricks();
  void resetBallToPaddle();
  void launchBall();
  void movePaddle(int8_t dx);
  static int16_t clamp16(int16_t v, int16_t lo, int16_t hi);

private:
  bool bricks_[BRICK_ROWS][BRICK_COLS] = {};
  uint8_t remainingBricks_ = 0;

  uint8_t paddleX_ = 0;

  int16_t ballXQ8_ = 0;
  int16_t ballYQ8_ = 0;
  int16_t velXQ8_ = 0;
  int16_t velYQ8_ = 0;

  uint8_t lives_ = 3;
  uint16_t score_ = 0;
  bool waitingLaunch_ = true;
  bool gameOver_ = false;
  bool won_ = false;

  uint32_t lastTickMs_ = 0;
  uint16_t accumMs_ = 0;
  uint8_t launchCount_ = 0;
};
