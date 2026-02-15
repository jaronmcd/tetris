#include "breakout.h"

static inline int16_t iabs16(int16_t v) {
  return (v < 0) ? (int16_t)(-v) : v;
}

// Breakout motion tuning (Q8.8 position space, per physics step).
static constexpr uint8_t BREAKOUT_PHYSICS_STEP_MS = 20; // was 16 (slower simulation rate)
static constexpr int16_t BREAKOUT_LAUNCH_VX[4] = {-64, -36, 36, 64}; // was {-88,-48,48,88}
static constexpr int16_t BREAKOUT_LAUNCH_VY = -124; // was -160
static constexpr int16_t BREAKOUT_MAX_ABS_VX = 160; // was 200
static constexpr int16_t BREAKOUT_MIN_ABS_VX = 18;  // was 24
static constexpr int16_t BREAKOUT_PADDLE_SPEEDUP_CAP_VY = -180; // was -220
static constexpr int16_t BREAKOUT_PADDLE_SPEEDUP_STEP = 1;      // was 4

int16_t BreakoutGame::clamp16(int16_t v, int16_t lo, int16_t hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

void BreakoutGame::begin(uint32_t nowMs) {
  reset(nowMs);
}

void BreakoutGame::resetBricks() {
  remainingBricks_ = 0;
  for (uint8_t y = 0; y < BRICK_ROWS; y++) {
    for (uint8_t x = 0; x < BRICK_COLS; x++) {
      bricks_[y][x] = true;
      remainingBricks_++;
    }
  }
}

void BreakoutGame::resetBallToPaddle() {
  const int16_t center = (int16_t)paddleX_ + (int16_t)((PADDLE_W - 1) / 2);
  ballXQ8_ = (int16_t)(center << 8);
  ballYQ8_ = (int16_t)((PADDLE_Y - 1) << 8);
  velXQ8_ = 0;
  velYQ8_ = 0;
}

void BreakoutGame::reset(uint32_t nowMs) {
  resetBricks();
  paddleX_ = (uint8_t)((MATRIX_W - PADDLE_W) / 2);

  lives_ = 3;
  score_ = 0;
  waitingLaunch_ = true;
  gameOver_ = false;
  won_ = false;
  launchCount_ = 0;

  lastTickMs_ = nowMs;
  accumMs_ = 0;
  resetBallToPaddle();
}

bool BreakoutGame::brickAt(uint8_t row, uint8_t col) const {
  if (row >= BRICK_ROWS || col >= BRICK_COLS) return false;
  return bricks_[row][col];
}

uint8_t BreakoutGame::ballX() const {
  int16_t px = (int16_t)((ballXQ8_ + 128) >> 8);
  px = clamp16(px, 0, MATRIX_W - 1);
  return (uint8_t)px;
}

uint8_t BreakoutGame::ballY() const {
  int16_t py = (int16_t)((ballYQ8_ + 128) >> 8);
  py = clamp16(py, 0, MATRIX_H - 1);
  return (uint8_t)py;
}

void BreakoutGame::movePaddle(int8_t dx) {
  int16_t x = (int16_t)paddleX_ + (int16_t)dx;
  x = clamp16(x, 0, MATRIX_W - PADDLE_W);
  paddleX_ = (uint8_t)x;
}

void BreakoutGame::launchBall() {
  if (!waitingLaunch_ || gameOver_) return;

  velXQ8_ = BREAKOUT_LAUNCH_VX[launchCount_ & 3u];
  velYQ8_ = BREAKOUT_LAUNCH_VY;
  waitingLaunch_ = false;
  launchCount_++;
}

BreakoutGame::TickResult BreakoutGame::stepFrame() {
  TickResult tr;
  if (waitingLaunch_ || gameOver_) return tr;

  const int16_t oldXQ8 = ballXQ8_;
  const int16_t oldYQ8 = ballYQ8_;

  ballXQ8_ = (int16_t)(ballXQ8_ + velXQ8_);
  ballYQ8_ = (int16_t)(ballYQ8_ + velYQ8_);

  const int16_t minXQ8 = 0;
  const int16_t maxXQ8 = (int16_t)((MATRIX_W - 1) << 8);
  const int16_t minYQ8 = 0;
  const int16_t maxYQ8 = (int16_t)((MATRIX_H - 1) << 8);

  if (ballXQ8_ < minXQ8) {
    ballXQ8_ = minXQ8;
    velXQ8_ = (int16_t)-velXQ8_;
  } else if (ballXQ8_ > maxXQ8) {
    ballXQ8_ = maxXQ8;
    velXQ8_ = (int16_t)-velXQ8_;
  }

  if (ballYQ8_ < minYQ8) {
    ballYQ8_ = minYQ8;
    velYQ8_ = iabs16(velYQ8_);
  }

  int16_t oldPx = (int16_t)((oldXQ8 + 128) >> 8);
  int16_t oldPy = (int16_t)((oldYQ8 + 128) >> 8);
  int16_t px = (int16_t)((ballXQ8_ + 128) >> 8);
  int16_t py = (int16_t)((ballYQ8_ + 128) >> 8);

  oldPx = clamp16(oldPx, 0, MATRIX_W - 1);
  oldPy = clamp16(oldPy, 0, MATRIX_H - 1);
  px = clamp16(px, 0, MATRIX_W - 1);
  py = clamp16(py, 0, MATRIX_H - 1);

  if (py >= 0 && py < BRICK_ROWS) {
    const uint8_t col = (uint8_t)(px / BRICK_W);
    if (col < BRICK_COLS && bricks_[py][col]) {
      bricks_[py][col] = false;
      if (remainingBricks_ > 0) remainingBricks_--;
      score_ = (uint16_t)(score_ + 10u);
      tr.brickBroken = true;

      const uint8_t oldCol = (uint8_t)(oldPx / BRICK_W);
      const bool crossedRow = (oldPy != py);
      const bool crossedCol = (oldCol != col);
      if (crossedRow || (!crossedCol && velYQ8_ != 0)) {
        velYQ8_ = (int16_t)-velYQ8_;
      } else {
        velXQ8_ = (int16_t)-velXQ8_;
      }

      if (velYQ8_ > 0) {
        ballYQ8_ = (int16_t)((py + 1) << 8);
      } else {
        ballYQ8_ = (py <= 0) ? 0 : (int16_t)((py - 1) << 8);
      }
    }
  }

  if (remainingBricks_ == 0) {
    won_ = true;
    gameOver_ = true;
    waitingLaunch_ = false;
    tr.won = true;
    tr.gameOver = true;
    return tr;
  }

  px = (int16_t)((ballXQ8_ + 128) >> 8);
  py = (int16_t)((ballYQ8_ + 128) >> 8);
  px = clamp16(px, 0, MATRIX_W - 1);
  py = clamp16(py, 0, MATRIX_H - 1);

  const int16_t paddleTop = (int16_t)PADDLE_Y - 1;
  if (velYQ8_ > 0 && py >= paddleTop) {
    const int16_t paddleLeft = (int16_t)paddleX_;
    const int16_t paddleRight = (int16_t)paddleX_ + (int16_t)PADDLE_W - 1;
    if (px >= paddleLeft && px <= paddleRight) {
      ballYQ8_ = (int16_t)(paddleTop << 8);
      velYQ8_ = (int16_t)-iabs16(velYQ8_);

      const int16_t paddleCenterQ8 = (int16_t)(((int16_t)paddleX_ << 8) + (((int16_t)PADDLE_W - 1) << 7));
      const int16_t offsetQ8 = (int16_t)(ballXQ8_ - paddleCenterQ8);
      velXQ8_ = (int16_t)(velXQ8_ + offsetQ8 / 7);

      if (velXQ8_ > BREAKOUT_MAX_ABS_VX) velXQ8_ = BREAKOUT_MAX_ABS_VX;
      if (velXQ8_ < -BREAKOUT_MAX_ABS_VX) velXQ8_ = -BREAKOUT_MAX_ABS_VX;

      if (iabs16(velXQ8_) < BREAKOUT_MIN_ABS_VX) {
        velXQ8_ = (velXQ8_ < 0) ? -BREAKOUT_MIN_ABS_VX : BREAKOUT_MIN_ABS_VX;
      }

      if (velYQ8_ > BREAKOUT_PADDLE_SPEEDUP_CAP_VY) {
        velYQ8_ = (int16_t)(velYQ8_ - BREAKOUT_PADDLE_SPEEDUP_STEP);
      }
    }
  }

  if (ballYQ8_ > maxYQ8) {
    tr.lostLife = true;
    if (lives_ > 0) lives_--;

    if (lives_ == 0) {
      gameOver_ = true;
      waitingLaunch_ = false;
      tr.gameOver = true;
      return tr;
    }

    waitingLaunch_ = true;
    resetBallToPaddle();
  }

  return tr;
}

BreakoutGame::TickResult BreakoutGame::tick(uint32_t nowMs, const Actions& a) {
  TickResult out;

  if (a.left) movePaddle(-1);
  if (a.right) movePaddle(+1);

  if (gameOver_) {
    return out;
  }

  if (waitingLaunch_) {
    resetBallToPaddle();
    if (a.rotate || a.drop || a.down) {
      launchBall();
    }
    lastTickMs_ = nowMs;
    accumMs_ = 0;
    return out;
  }

  if (lastTickMs_ == 0) {
    lastTickMs_ = nowMs;
    return out;
  }

  uint32_t delta = nowMs - lastTickMs_;
  lastTickMs_ = nowMs;
  if (delta > 100) delta = 100;

  accumMs_ = (uint16_t)(accumMs_ + delta);
  while (accumMs_ >= BREAKOUT_PHYSICS_STEP_MS) {
    accumMs_ -= BREAKOUT_PHYSICS_STEP_MS;
    TickResult tr = stepFrame();
    if (tr.brickBroken) out.brickBroken = true;
    if (tr.lostLife) out.lostLife = true;
    if (tr.won) out.won = true;
    if (tr.gameOver) out.gameOver = true;

    if (gameOver_ || waitingLaunch_) break;
  }

  return out;
}
