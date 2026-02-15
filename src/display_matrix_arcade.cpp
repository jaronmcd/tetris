#include "display_matrix.h"
#include "breakout.h"

static inline uint8_t tri8u(uint8_t v) {
  return (v & 0x80) ? (uint8_t)(255 - ((v & 0x7F) << 1)) : (uint8_t)((v & 0x7F) << 1);
}

void MatrixDisplay::renderGameSelectMenu(uint8_t selectedGame, uint32_t nowMs) {
  if (selectedGame > 1) selectedGame = 1;
  strip_.clear();

  for (uint8_t y = 0; y < MATRIX_H; y++) {
    for (uint8_t x = 0; x < MATRIX_W; x++) {
      const bool leftPanel = (x < (MATRIX_W / 2));
      const uint16_t hue = leftPanel
        ? (uint16_t)(41000u + (uint16_t)(x * 400u) + (uint16_t)(y * 240u) + (uint16_t)(nowMs / 7u))
        : (uint16_t)(3600u + (uint16_t)(x * 360u) + (uint16_t)(y * 180u) + (uint16_t)(nowMs / 6u));
      const uint8_t val = leftPanel ? (uint8_t)(8u + (y >> 1)) : (uint8_t)(9u + (y >> 1));
      setPixel(x, y, strip_.ColorHSV(hue, 210, val));
    }
  }

  const uint32_t split = strip_.Color(30, 30, 34);
  for (uint8_t y = 0; y < MATRIX_H; y++) {
    setPixel((MATRIX_W / 2) - 1, y, split);
    setPixel((MATRIX_W / 2), y, split);
  }

  // Left icon: mini "falling block" scene (Tetris)
  const uint32_t tStack = strip_.Color(45, 160, 255);
  const uint32_t tPiece = strip_.Color(255, 190, 40);
  for (uint8_t x = 1; x <= 6; x++) {
    setPixel(x, 13, tStack);
  }
  setPixel(1, 12, tStack);
  setPixel(2, 12, tStack);
  setPixel(5, 12, tStack);
  setPixel(6, 12, tStack);

  const uint8_t drop = (uint8_t)((nowMs / 170u) % 6u);
  const uint8_t py = (uint8_t)(4u + drop);
  setPixel(2, py, tPiece);
  setPixel(3, py, tPiece);
  setPixel(4, py, tPiece);
  setPixel(3, (uint8_t)(py + 1u), tPiece);
  drawChar(2, 1, 'T', strip_.Color(180, 200, 255));

  // Right icon: mini Breakout scene
  const uint32_t bBrickA = strip_.Color(255, 110, 70);
  const uint32_t bBrickB = strip_.Color(255, 180, 60);
  for (uint8_t ry = 3; ry <= 5; ry++) {
    for (uint8_t x = 9; x <= 14; x++) {
      const uint32_t c = ((x + ry) & 1) ? bBrickA : bBrickB;
      setPixel(x, ry, c);
    }
  }

  const uint8_t paddleX = (uint8_t)(9u + ((nowMs / 180u) % 4u));
  for (uint8_t dx = 0; dx < 4; dx++) {
    setPixel((uint8_t)(paddleX + dx), 13, strip_.Color(80, 255, 230));
  }

  const uint8_t ballPhase = (uint8_t)((nowMs / 120u) % 7u);
  const uint8_t ballX = (uint8_t)(9u + ballPhase);
  const uint8_t ballY = (uint8_t)(11u - (ballPhase >> 1));
  setPixel(ballX, ballY, strip_.Color(255, 255, 255));

  const uint8_t pulse = (uint8_t)(120u + (tri8u((uint8_t)(nowMs / 6u)) >> 2));
  const uint32_t hi = strip_.Color(pulse, pulse, pulse);
  const uint32_t lo = strip_.Color(35, 35, 35);

  const uint8_t leftX0 = 0;
  const uint8_t leftX1 = (MATRIX_W / 2) - 1;
  const uint8_t rightX0 = MATRIX_W / 2;
  const uint8_t rightX1 = MATRIX_W - 1;

  const bool leftSel = (selectedGame == 0);
  for (uint8_t x = leftX0; x <= leftX1; x++) {
    setPixel(x, 0, leftSel ? hi : lo);
    setPixel(x, MATRIX_H - 1, leftSel ? hi : lo);
  }
  for (uint8_t y = 0; y < MATRIX_H; y++) {
    setPixel(leftX0, y, leftSel ? hi : lo);
    setPixel(leftX1, y, leftSel ? hi : lo);
  }

  const bool rightSel = !leftSel;
  for (uint8_t x = rightX0; x <= rightX1; x++) {
    setPixel(x, 0, rightSel ? hi : lo);
    setPixel(x, MATRIX_H - 1, rightSel ? hi : lo);
  }
  for (uint8_t y = 0; y < MATRIX_H; y++) {
    setPixel(rightX0, y, rightSel ? hi : lo);
    setPixel(rightX1, y, rightSel ? hi : lo);
  }

  if (((nowMs / 360u) & 1u) == 0u) {
    setPixel(7, 15, strip_.Color(190, 190, 190));
    setPixel(8, 15, strip_.Color(190, 190, 190));
  }

  strip_.show();
}

void MatrixDisplay::renderBreakout(const BreakoutGame& g, uint32_t nowMs) {
  strip_.clear();

  for (uint8_t y = 0; y < MATRIX_H; y++) {
    const uint8_t v = (uint8_t)(3u + (y >> 1));
    for (uint8_t x = 0; x < MATRIX_W; x++) {
      const uint16_t hue = (uint16_t)(45000u + (uint16_t)(x * 180u) + (uint16_t)(nowMs / 11u));
      setPixel(x, y, strip_.ColorHSV(hue, 220, v));
    }
  }

  // Bricks: row-colored strips across the top.
  for (uint8_t row = 0; row < BreakoutGame::BRICK_ROWS; row++) {
    const uint16_t hue = (uint16_t)(5000u + (uint16_t)row * 3500u + (uint16_t)(nowMs / 20u));
    const uint32_t c = strip_.ColorHSV(hue, 235, 200);
    for (uint8_t col = 0; col < BreakoutGame::BRICK_COLS; col++) {
      if (!g.brickAt(row, col)) continue;
      const uint8_t x0 = (uint8_t)(col * BreakoutGame::BRICK_W);
      for (uint8_t dx = 0; dx < BreakoutGame::BRICK_W; dx++) {
        setPixel((uint8_t)(x0 + dx), row, c);
      }
    }
  }

  // Paddle with gentle pulse.
  const uint8_t paddlePulse = (uint8_t)(180u + (tri8u((uint8_t)(nowMs / 5u)) >> 3));
  const uint32_t paddleC = strip_.Color(70, paddlePulse, 230);
  for (uint8_t dx = 0; dx < g.paddleWidth(); dx++) {
    const uint8_t x = (uint8_t)(g.paddleX() + dx);
    if (x < MATRIX_W) {
      setPixel(x, BreakoutGame::PADDLE_Y, paddleC);
    }
  }

  bool drawBall = true;
  if (g.waitingLaunch() && ((nowMs / 220u) & 1u)) {
    drawBall = false;
  }
  if (drawBall) {
    const uint8_t bx = g.ballX();
    const uint8_t by = g.ballY();
    setPixel(bx, by, strip_.Color(255, 255, 255));
  }

  if (g.isGameOver()) {
    for (uint8_t y = 5; y <= 11; y++) {
      for (uint8_t x = 1; x <= 14; x++) {
        setPixel(x, y, strip_.Color(0, 0, 0));
      }
    }

    // Wordless end-state indicator: green bar for win, red bar for loss.
    const uint32_t stateC = g.won() ? strip_.Color(60, 255, 120)
                                    : strip_.Color(255, 60, 60);
    for (uint8_t y = 7; y <= 8; y++) {
      for (uint8_t x = 5; x <= 10; x++) {
        setPixel(x, y, stateC);
      }
    }
    if (((nowMs / 280u) & 1u) == 0u) {
      setPixel(7, 10, strip_.Color(255, 255, 255));
      setPixel(8, 10, strip_.Color(255, 255, 255));
    }
  }

  strip_.show();
}
