#include "display_matrix.h"
#include "breakout.h"

static inline uint8_t tri8u(uint8_t v) {
  return (v & 0x80) ? (uint8_t)(255 - ((v & 0x7F) << 1)) : (uint8_t)((v & 0x7F) << 1);
}

void MatrixDisplay::renderSettingsScreen(uint32_t nowMs) {
  strip_.clear();

  const uint8_t brightnessPct = (uint8_t)(((uint16_t)userBrightness_ * 100u) / 255u);
  const String pctText = String((int)brightnessPct);

  // Numeric value on the left.
  const int16_t valueY = 7;
  const int16_t textWidth = (int16_t)pctText.length() * 4 - 1;
  const int16_t valueAreaW = 11;  // x=0..10
  int16_t valueX = (valueAreaW - textWidth) / 2;
  if (valueX < 0) valueX = 0;
  drawText(valueX, valueY, pctText, strip_.Color(240, 240, 255));

  // Brightness bar on the right (side-by-side with the numeric value).
  const uint8_t barX0 = 11;
  const uint8_t barX1 = 15;
  const uint8_t barY0 = 3;
  const uint8_t barY1 = 14;
  const uint32_t frameColor = strip_.Color(24, 24, 30);
  const uint32_t fillOn = strip_.Color(85, 240, 180);
  const uint32_t fillOff = strip_.Color(20, 20, 26);

  for (uint8_t x = barX0; x <= barX1; x++) {
    setPixel(x, barY0, frameColor);
    setPixel(x, barY1, frameColor);
  }
  for (uint8_t y = barY0; y <= barY1; y++) {
    setPixel(barX0, y, frameColor);
    setPixel(barX1, y, frameColor);
  }

  const uint8_t meterRows = (uint8_t)(barY1 - barY0 - 1);  // 10 rows
  const uint8_t filledRows = (uint8_t)(((uint16_t)brightnessPct * meterRows + 99u) / 100u);
  for (uint8_t row = 0; row < meterRows; row++) {
    const uint32_t c = (row < filledRows) ? fillOn : fillOff;
    const uint8_t y = (uint8_t)((barY1 - 1) - row);
    for (uint8_t x = (uint8_t)(barX0 + 1); x < barX1; x++) {
      setPixel(x, y, c);
    }
  }

  // Tiny pulse under the bar as feedback while adjusting.
  const uint32_t hint = (((nowMs / 220u) & 1u) != 0u)
                      ? strip_.Color(220, 220, 220)
                      : strip_.Color(45, 45, 55);
  setPixel(13, 15, hint);

  // Keep rotation visible, but minimal: tiny corner marker only.
  const uint32_t marker = strip_.Color(170, 190, 255);
  switch (rotationQuarterTurns_ & 0x03u) {
    case 1:
      setPixel(7, 0, marker);
      break;
    case 2:
      setPixel(7, 15, marker);
      break;
    case 3:
      setPixel(0, 7, marker);
      break;
    default:
      setPixel(0, 0, marker);
      break;
  }

  strip_.show();
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

  // Left icon: mini "falling block" scene (Tetris), no text glyph.
  const uint32_t tStackA = strip_.Color(45, 160, 255);
  const uint32_t tStackB = strip_.Color(30, 115, 225);
  const uint32_t tPiece = strip_.Color(255, 190, 40);

  for (uint8_t x = 1; x <= 6; x++) {
    setPixel(x, 13, ((x & 1u) == 0u) ? tStackA : tStackB);
  }
  setPixel(1, 12, tStackA);
  setPixel(2, 12, tStackB);
  setPixel(4, 12, tStackA);
  setPixel(5, 12, tStackB);
  setPixel(2, 11, tStackA);
  setPixel(5, 11, tStackB);

  const uint8_t pieceKind = (uint8_t)((nowMs / 1200u) % 4u); // I, T, L, O
  const int8_t sway = (int8_t)((nowMs / 480u) % 3u) - 1;      // -1, 0, +1
  const uint8_t drop = (uint8_t)((nowMs / 160u) % 8u);
  const int8_t by = (int8_t)(2 + drop);
  int8_t bx = 3 + sway;

  if (pieceKind == 0 && bx > 2) bx = 2; // keep I-piece inside the panel

  auto drawMiniBlock = [&](int8_t x, int8_t y) {
    if (x >= 1 && x <= 6 && y >= 1 && y <= 13) {
      setPixel((uint8_t)x, (uint8_t)y, tPiece);
    }
  };

  switch (pieceKind) {
    case 0: // I
      drawMiniBlock(bx + 0, by);
      drawMiniBlock(bx + 1, by);
      drawMiniBlock(bx + 2, by);
      drawMiniBlock(bx + 3, by);
      break;
    case 1: // T
      drawMiniBlock(bx - 1, by);
      drawMiniBlock(bx + 0, by);
      drawMiniBlock(bx + 1, by);
      drawMiniBlock(bx + 0, by + 1);
      break;
    case 2: // L
      drawMiniBlock(bx - 1, by);
      drawMiniBlock(bx + 0, by);
      drawMiniBlock(bx + 1, by);
      drawMiniBlock(bx + 1, by + 1);
      break;
    default: // O
      drawMiniBlock(bx + 0, by);
      drawMiniBlock(bx + 1, by);
      drawMiniBlock(bx + 0, by + 1);
      drawMiniBlock(bx + 1, by + 1);
      break;
  }

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

    auto addGlow = [&](int16_t x, int16_t y, uint8_t ar, uint8_t ag, uint8_t ab) {
      if (x < 0 || x >= (int16_t)MATRIX_W || y < 0 || y >= (int16_t)MATRIX_H) return;
      const uint16_t idx = XY((uint8_t)x, (uint8_t)y);
      uint32_t old = strip_.getPixelColor(idx);
      uint16_t r = (uint16_t)((old >> 16) & 0xFF) + ar;
      uint16_t gch = (uint16_t)((old >> 8) & 0xFF) + ag;
      uint16_t bch = (uint16_t)(old & 0xFF) + ab;
      if (r > 255) r = 255;
      if (gch > 255) gch = 255;
      if (bch > 255) bch = 255;
      strip_.setPixelColor(idx, strip_.Color((uint8_t)r, (uint8_t)gch, (uint8_t)bch));
    };

    const int16_t vx = g.ballVelXQ8();
    const int16_t vy = g.ballVelYQ8();
    const int16_t sx = (vx > 10) ? 1 : (vx < -10 ? -1 : 0);
    const int16_t sy = (vy > 10) ? 1 : (vy < -10 ? -1 : 0);

    // Unlocked at level 10: velocity-only artifact (no static halo).
    if (g.level() >= BreakoutGame::MAX_LEVEL && (sx != 0 || sy != 0)) {
      uint16_t speed = (uint16_t)(abs(vx) + abs(vy));
      if (speed > 180) speed = 180;

      uint8_t t1r = (uint8_t)(4u + (speed / 12u));
      uint8_t t1g = (uint8_t)(8u + (speed / 8u));
      uint8_t t1b = (uint8_t)(14u + (speed / 5u));

      uint8_t t2r = (uint8_t)(2u + (speed / 26u));
      uint8_t t2g = (uint8_t)(4u + (speed / 18u));
      uint8_t t2b = (uint8_t)(7u + (speed / 12u));

      addGlow((int16_t)bx - sx, (int16_t)by - sy, t1r, t1g, t1b);
      addGlow((int16_t)bx - (2 * sx), (int16_t)by - (2 * sy), t2r, t2g, t2b);
    }

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
