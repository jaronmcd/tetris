#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <Bluepad32.h>
#include "esp_system.h"

// ======================
// NeoPixel (your known-good pin)
// ======================
#define LED_PIN 1

#define MATRIX_W 16
#define MATRIX_H 16
#define NUM_LEDS (MATRIX_W * MATRIX_H)

#define BRIGHTNESS 14
#define SERPENTINE true
#define MATRIX_BOTTOM_UP false

// ======================
// Tetris board (10x16 centered on 16x16)
// ======================
#define BOARD_W 10
#define BOARD_H 16
#define BOARD_OFFSET_X 3
#define BOARD_OFFSET_Y 0

// ======================
// Bluepad32 DPAD bits (matches what you're seeing)
// ======================
#define DPAD_UP    0x01
#define DPAD_DOWN  0x02
#define DPAD_LEFT  0x04
#define DPAD_RIGHT 0x08

// ---------- NeoPixel ----------
Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

static inline uint16_t XY(uint8_t x, uint8_t y) {
  if (x >= MATRIX_W || y >= MATRIX_H) return 0;
  uint8_t yy = MATRIX_BOTTOM_UP ? (MATRIX_H - 1 - y) : y;

  if (!SERPENTINE) return (uint16_t)yy * MATRIX_W + x;

  if ((yy & 1) == 0) return (uint16_t)yy * MATRIX_W + x;
  return (uint16_t)yy * MATRIX_W + (MATRIX_W - 1 - x);
}

static inline void setPixel(uint8_t x, uint8_t y, uint32_t c) {
  strip.setPixelColor(XY(x, y), c);
}

static inline uint32_t rgb(uint32_t rrggbb) {
  uint8_t r = (rrggbb >> 16) & 0xFF;
  uint8_t g = (rrggbb >> 8) & 0xFF;
  uint8_t b = (rrggbb) & 0xFF;
  return strip.Color(r, g, b);
}

// ---------- Tetris ----------
static uint8_t board[BOARD_H][BOARD_W]; // 0 empty, 1..7 piece id

enum PieceType : uint8_t { I=0, O, T, S, Z, J, L };

struct Piece {
  uint8_t type = 0;
  int8_t x = 3;
  int8_t y = -1;
  uint8_t rot = 0;
};

static Piece cur;
static bool gameOver = false;

static uint32_t score = 0;
static uint16_t linesCleared = 0;
static uint8_t level = 1;

static uint32_t lastFallMs = 0;
static uint32_t lastFrameMs = 0;

// 4x4 bitmasks (bit 15 = (0,0) ... bit 0 = (3,3))
static inline bool maskCell(uint16_t m, uint8_t r, uint8_t c) {
  uint8_t bit = 15 - (r * 4 + c);
  return (m >> bit) & 1;
}

// Piece masks
static const uint16_t SHAPES[7][4] = {
  { 0x0F00, 0x2222, 0x00F0, 0x4444 }, // I
  { 0x6600, 0x6600, 0x6600, 0x6600 }, // O
  { 0x0E40, 0x4C40, 0x4E00, 0x4640 }, // T
  { 0x06C0, 0x8C40, 0x06C0, 0x8C40 }, // S
  { 0x0C60, 0x4C80, 0x0C60, 0x4C80 }, // Z
  { 0x8E00, 0x6440, 0x0E20, 0x44C0 }, // J
  { 0x2E00, 0x4460, 0x0E80, 0xC440 }  // L
};

static const uint32_t PIECE_COLORS[8] = {
  0,
  0x00FFFF, // I
  0xFFFF00, // O
  0xAA00FF, // T
  0x00FF00, // S
  0xFF0000, // Z
  0x0000FF, // J
  0xFF7F00  // L
};

static bool fits(uint8_t type, uint8_t rot, int8_t px, int8_t py) {
  uint16_t m = SHAPES[type][rot & 3];
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

static void placePieceToBoard(const Piece& p) {
  uint16_t m = SHAPES[p.type][p.rot & 3];
  uint8_t id = (uint8_t)(p.type + 1);
  for (uint8_t r = 0; r < 4; r++) {
    for (uint8_t c = 0; c < 4; c++) {
      if (!maskCell(m, r, c)) continue;
      int8_t bx = p.x + (int8_t)c;
      int8_t by = p.y + (int8_t)r;
      if (by < 0) continue;
      if (bx >= 0 && bx < BOARD_W && by >= 0 && by < BOARD_H) {
        board[by][bx] = id;
      }
    }
  }
}

static uint8_t bag[7];
static uint8_t bagIdx = 7;

static void refillBag() {
  for (uint8_t i = 0; i < 7; i++) bag[i] = i;
  for (int i = 6; i > 0; i--) {
    uint32_t r = esp_random();
    int j = r % (i + 1);
    uint8_t tmp = bag[i];
    bag[i] = bag[j];
    bag[j] = tmp;
  }
  bagIdx = 0;
}
static uint8_t nextPieceType() {
  if (bagIdx >= 7) refillBag();
  return bag[bagIdx++];
}

static void updateLevel() {
  level = (linesCleared / 10) + 1;
}

static uint32_t dropIntervalMs() {
  int base = 700;
  int dec = (int)(level - 1) * 45;
  int ms = base - dec;
  if (ms < 90) ms = 90;
  return (uint32_t)ms;
}

static void clearLines() {
  uint8_t clearedThis = 0;

  for (int y = BOARD_H - 1; y >= 0; y--) {
    bool full = true;
    for (uint8_t x = 0; x < BOARD_W; x++) {
      if (board[y][x] == 0) { full = false; break; }
    }
    if (full) {
      clearedThis++;
      for (int yy = y; yy > 0; yy--) {
        memcpy(board[yy], board[yy - 1], BOARD_W);
      }
      memset(board[0], 0, BOARD_W);
      y++;
    }
  }

  if (clearedThis > 0) {
    linesCleared += clearedThis;
    updateLevel();
    uint32_t add = 0;
    switch (clearedThis) {
      case 1: add = 40; break;
      case 2: add = 100; break;
      case 3: add = 300; break;
      default: add = 1200; break;
    }
    score += add * level;
  }
}

static void spawnNext() {
  cur.type = nextPieceType();
  cur.rot = 0;
  cur.x = 3;
  cur.y = -1;
  if (!fits(cur.type, cur.rot, cur.x, cur.y)) gameOver = true;
}

static void lockAndContinue() {
  placePieceToBoard(cur);
  clearLines();
  spawnNext();
}

static void tryMove(int8_t dx, int8_t dy) {
  int8_t nx = cur.x + dx;
  int8_t ny = cur.y + dy;
  if (fits(cur.type, cur.rot, nx, ny)) {
    cur.x = nx;
    cur.y = ny;
  } else if (dy == 1) {
    lockAndContinue();
  }
}

static void tryRotate() {
  uint8_t nr = (cur.rot + 1) & 3;
  if (fits(cur.type, nr, cur.x, cur.y)) { cur.rot = nr; return; }
  if (fits(cur.type, nr, cur.x - 1, cur.y)) { cur.x -= 1; cur.rot = nr; return; }
  if (fits(cur.type, nr, cur.x + 1, cur.y)) { cur.x += 1; cur.rot = nr; return; }
}

static void hardDrop() {
  while (fits(cur.type, cur.rot, cur.x, cur.y + 1)) {
    cur.y += 1;
    score += 1;
  }
  lockAndContinue();
}

static void resetGame() {
  memset(board, 0, sizeof(board));
  score = 0;
  linesCleared = 0;
  level = 1;
  gameOver = false;
  bagIdx = 7;
  refillBag();
  spawnNext();
  lastFallMs = millis();
}

// ---------- Rendering ----------
static void render() {
  strip.clear();

  // dim border outside board so you can see orientation
  uint32_t border = strip.Color(2, 2, 2);
  for (uint8_t y = 0; y < MATRIX_H; y++) {
    for (uint8_t x = 0; x < MATRIX_W; x++) {
      bool inBoardX = (x >= BOARD_OFFSET_X) && (x < (BOARD_OFFSET_X + BOARD_W));
      bool inBoardY = (y >= BOARD_OFFSET_Y) && (y < (BOARD_OFFSET_Y + BOARD_H));
      if (!inBoardX || !inBoardY) {
        setPixel(x, y, border);
      }
    }
  }

  // board
  for (uint8_t by = 0; by < BOARD_H; by++) {
    for (uint8_t bx = 0; bx < BOARD_W; bx++) {
      uint8_t id = board[by][bx];
      if (id) {
        setPixel(BOARD_OFFSET_X + bx, BOARD_OFFSET_Y + by, rgb(PIECE_COLORS[id]));
      }
    }
  }

  // current piece
  if (!gameOver) {
    uint16_t m = SHAPES[cur.type][cur.rot & 3];
    uint8_t id = (uint8_t)(cur.type + 1);
    for (uint8_t r = 0; r < 4; r++) {
      for (uint8_t c = 0; c < 4; c++) {
        if (!maskCell(m, r, c)) continue;
        int8_t bx = cur.x + (int8_t)c;
        int8_t by = cur.y + (int8_t)r;
        if (by < 0) continue;
        if (bx < 0 || bx >= BOARD_W || by >= BOARD_H) continue;
        setPixel(BOARD_OFFSET_X + (uint8_t)bx, BOARD_OFFSET_Y + (uint8_t)by, rgb(PIECE_COLORS[id]));
      }
    }
  } else {
    bool on = ((millis() / 350) & 1) == 0;
    uint32_t c = on ? strip.Color(40, 0, 0) : strip.Color(0, 0, 0);
    for (uint8_t y = 0; y < MATRIX_H; y++)
      for (uint8_t x = 0; x < MATRIX_W; x++)
        setPixel(x, y, c);
  }

  strip.show();
}

// ---------- Serial + Gamepad actions ----------
struct Actions {
  bool left = false;
  bool right = false;
  bool rotate = false;
  bool down = false;   // held for soft drop
  bool drop = false;   // hard drop
  bool restart = false;
};

static Actions serialActions() {
  Actions a;
  while (Serial.available() > 0) {
    char ch = (char)Serial.read();
    if (ch == 'a' || ch == 'A') a.left = true;
    else if (ch == 'd' || ch == 'D') a.right = true;
    else if (ch == 'w' || ch == 'W') a.rotate = true;
    else if (ch == 's' || ch == 'S') a.down = true;
    else if (ch == ' ') a.drop = true;
    else if (ch == 'r' || ch == 'R') a.restart = true;
  }
  return a;
}

struct RepeatKey {
  bool lastHeld = false;
  uint32_t nextMs = 0;
};

static bool fireWithRepeat(bool held, RepeatKey &rk, uint32_t now,
                           uint32_t firstDelayMs, uint32_t repeatMs) {
  if (!held) {
    rk.lastHeld = false;
    rk.nextMs = 0;
    return false;
  }
  if (!rk.lastHeld) {
    rk.lastHeld = true;
    rk.nextMs = now + firstDelayMs;
    return true;
  }
  if (now >= rk.nextMs) {
    rk.nextMs = now + repeatMs;
    return true;
  }
  return false;
}

// ---------- Bluepad32 ----------
static GamepadPtr gp = nullptr;

static void onConnected(GamepadPtr g) {
  gp = g;
  Serial.println("Gamepad connected!");
}

static void onDisconnected(GamepadPtr) {
  gp = nullptr;
  Serial.println("Gamepad disconnected!");
}

static Actions gamepadActions() {
  Actions a;
  if (!gp || !gp->isConnected()) return a;

  uint8_t d = gp->dpad();
  bool leftHeld  = d & DPAD_LEFT;
  bool rightHeld = d & DPAD_RIGHT;
  bool downHeld  = d & DPAD_DOWN;

  uint32_t now = millis();
  static RepeatKey repL, repR;
  // Left/right: initial move immediate, then repeat like classic DAS-ish
  if (fireWithRepeat(leftHeld,  repL, now, 170, 90)) a.left = true;
  if (fireWithRepeat(rightHeld, repR, now, 170, 90)) a.right = true;

  // Down: treated as held (soft drop)
  a.down = downHeld;

  // Edge-trigger buttons
  static bool lastA = false, lastB = false, lastY = false;
  bool A = gp->a();
  bool B = gp->b();
  bool Y = gp->y();

  if (A && !lastA) a.rotate = true;
  if (B && !lastB) a.drop = true;
  if (Y && !lastY) a.restart = true;

  lastA = A; lastB = B; lastY = Y;
  return a;
}

void setup() {
  Serial.begin(115200);
  delay(200);

  strip.begin();
  strip.setBrightness(BRIGHTNESS);
  strip.clear();
  strip.show();

  // Quick boot flash so you KNOW pixels are being written
  strip.fill(strip.Color(20, 0, 0)); strip.show(); delay(120);
  strip.fill(strip.Color(0, 20, 0)); strip.show(); delay(120);
  strip.fill(strip.Color(0, 0, 20)); strip.show(); delay(120);
  strip.clear(); strip.show();

  BP32.setup(&onConnected, &onDisconnected);
  BP32.enableNewBluetoothConnections(true);

  resetGame();

  Serial.println("\nTetris ready. Xbox: D-pad move, A=rotate, B=drop, Y=restart.");
  Serial.println("Serial: a/d/w/s/space, r=restart");
}

void loop() {
  BP32.update();

  Actions s = serialActions();
  Actions g = gamepadActions();

  Actions act;
  act.left = s.left || g.left;
  act.right = s.right || g.right;
  act.rotate = s.rotate || g.rotate;
  act.down = s.down || g.down;
  act.drop = s.drop || g.drop;
  act.restart = s.restart || g.restart;

  if (act.restart) resetGame();

  if (!gameOver) {
    if (act.left) tryMove(-1, 0);
    if (act.right) tryMove(+1, 0);
    if (act.rotate) tryRotate();

    uint32_t now = millis();
    if (act.drop) {
      hardDrop();
    } else if (act.down) {
      // soft drop faster while held
      if ((now - lastFallMs) >= 60) {
        lastFallMs = now;
        tryMove(0, +1);
      }
    } else {
      if ((now - lastFallMs) >= dropIntervalMs()) {
        lastFallMs = now;
        tryMove(0, +1);
      }
    }
  }

  uint32_t now = millis();
  if ((now - lastFrameMs) >= 33) {
    lastFrameMs = now;
    render();
  }
}
