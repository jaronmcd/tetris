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
// Bluepad32 DPAD bits (matches your logs)
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

// ---------- Themes / Palettes ----------
static constexpr uint8_t NUM_THEMES = 5;

// THEMES[theme][pieceId 0..7], rrggbb
static const uint32_t THEMES[NUM_THEMES][8] = {
  // 0) Classic
  {0, 0x00FFFF, 0xFFFF00, 0xAA00FF, 0x00FF00, 0xFF0000, 0x0000FF, 0xFF7F00},
  // 1) Neon
  {0, 0x00F0FF, 0xFFD000, 0xFF00FF, 0x00FF66, 0xFF0066, 0x3B6CFF, 0xFF4D00},
  // 2) Ice
  {0, 0xA0FFFF, 0xE8F7FF, 0x80A0FF, 0x60FFCC, 0xFF80A0, 0x60A0FF, 0xA0D8FF},
  // 3) Lava
  {0, 0xFFAA00, 0xFFDD00, 0xFF3300, 0xFF6600, 0xCC0000, 0xFF8800, 0xFF4400},
  // 4) Synthwave
  {0, 0x00E5FF, 0xFFF400, 0xFF3DF2, 0x00FF9A, 0xFF2D55, 0x6C63FF, 0xFF7A00},
};

static inline uint8_t themeIndex() {
  // stage changes every level. Want slower? use: (level-1)/2
  return (uint8_t)((level - 1) % NUM_THEMES);
}

static inline uint32_t pieceColor(uint8_t id /*1..7*/) {
  return rgb(THEMES[themeIndex()][id]);
}

// ---------- Level-up flash ----------
static void doLevelUpFlash() {
  // quick “arcade” white flashes without being too blinding
  uint8_t oldB = strip.getBrightness();

  strip.setBrightness(40);
  strip.fill(strip.Color(80, 80, 80));
  strip.show();
  delay(55);

  strip.clear();
  strip.show();
  delay(35);

  strip.fill(strip.Color(80, 80, 80));
  strip.show();
  delay(55);

  strip.setBrightness(oldB);
  strip.clear();
  strip.show();
}

// ---------- Core gameplay ----------
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
    int j = (int)(r % (i + 1));
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

    uint8_t oldLevel = level;
    updateLevel();
    if (level != oldLevel) {
      doLevelUpFlash();
      Serial.printf("Level %u (theme %u)\n", level, themeIndex());
    }

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

// ---------- Arcade border helpers ----------
static inline uint8_t borderTimeShiftForLevel() {
  // Smaller shift => faster animation as you level up
  if (level <= 1) return 6;
  if (level <= 3) return 5;
  if (level <= 6) return 4;
  if (level <= 10) return 3;
  if (level <= 15) return 2;
  return 1;
}

// 0..255 triangle wave (no floats)
static inline uint8_t tri8(uint8_t x) {
  return (x & 0x80) ? (uint8_t)(255 - ((x & 0x7F) << 1)) : (uint8_t)((x & 0x7F) << 1);
}

// Classic NeoPixel wheel: pos 0..255 -> RGB
static inline void wheel(uint8_t pos, uint8_t &r, uint8_t &g, uint8_t &b) {
  pos = 255 - pos;
  if (pos < 85) {
    r = 255 - pos * 3; g = 0;          b = pos * 3;
  } else if (pos < 170) {
    pos -= 85;
    r = 0;          g = pos * 3;       b = 255 - pos * 3;
  } else {
    pos -= 170;
    r = pos * 3;    g = 255 - pos * 3; b = 0;
  }
}

static inline uint32_t arcadeBorderColor(uint8_t x, uint8_t y, uint32_t nowMs) {
  // Distance from playable board area (0 = inside board)
  int dx = 0, dy = 0;

  if (x < BOARD_OFFSET_X) dx = BOARD_OFFSET_X - x;
  else if (x >= (BOARD_OFFSET_X + BOARD_W)) dx = x - (BOARD_OFFSET_X + BOARD_W - 1);

  if (y < BOARD_OFFSET_Y) dy = BOARD_OFFSET_Y - y;
  else if (y >= (BOARD_OFFSET_Y + BOARD_H)) dy = y - (BOARD_OFFSET_Y + BOARD_H - 1);

  int dist = max(dx, dy);
  if (dist <= 0) return 0; // inside board

  // Max border thickness around board
  const int maxDist =
      max(max(BOARD_OFFSET_X, (MATRIX_W - (BOARD_OFFSET_X + BOARD_W))),
          max(BOARD_OFFSET_Y, (MATRIX_H - (BOARD_OFFSET_Y + BOARD_H))));

  uint8_t tShift = borderTimeShiftForLevel();
  uint8_t theme = themeIndex();
  uint8_t themeOffset = (uint8_t)(theme * 48);

  // Hue: position + time + theme offset, speed increases with level
  uint8_t hue = (uint8_t)(x * 17 + y * 23 + (nowMs >> tShift) + themeOffset);

  uint8_t r, g, b;
  wheel(hue, r, g, b);

  // Fade outward: near board bright, farther out dim
  uint8_t fade = 70;
  if (maxDist > 1) {
    fade = (uint8_t)(70 + (185 * (maxDist - (dist - 1))) / maxDist);
  } else {
    fade = 255;
  }

  // Gentle pulse: 190..255
  uint8_t pulse = (uint8_t)(190 + (tri8((uint8_t)(nowMs >> 5)) >> 2));

  uint16_t scale16 = (uint16_t)fade * (uint16_t)pulse;
  uint8_t scale = (uint8_t)(scale16 >> 8);

  r = (uint8_t)(((uint16_t)r * scale) >> 8);
  g = (uint8_t)(((uint16_t)g * scale) >> 8);
  b = (uint8_t)(((uint16_t)b * scale) >> 8);

  return strip.Color(r, g, b);
}

// ---------- Rendering ----------
static void render() {
  strip.clear();

  uint32_t nowMs = millis();

  // Border: animated arcade rainbow, fades outward
  for (uint8_t y = 0; y < MATRIX_H; y++) {
    for (uint8_t x = 0; x < MATRIX_W; x++) {
      bool inBoardX = (x >= BOARD_OFFSET_X) && (x < (BOARD_OFFSET_X + BOARD_W));
      bool inBoardY = (y >= BOARD_OFFSET_Y) && (y < (BOARD_OFFSET_Y + BOARD_H));
      if (!inBoardX || !inBoardY) {
        setPixel(x, y, arcadeBorderColor(x, y, nowMs));
      }
    }
  }

  // Locked blocks
  for (uint8_t by = 0; by < BOARD_H; by++) {
    for (uint8_t bx = 0; bx < BOARD_W; bx++) {
      uint8_t id = board[by][bx];
      if (id) {
        setPixel(BOARD_OFFSET_X + bx, BOARD_OFFSET_Y + by, pieceColor(id));
      }
    }
  }

  // Current piece
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
        setPixel(BOARD_OFFSET_X + (uint8_t)bx, BOARD_OFFSET_Y + (uint8_t)by, pieceColor(id));
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

// ---------- Input ----------
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
    return true; // immediate on press
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

  if (fireWithRepeat(leftHeld,  repL, now, 170, 90)) a.left = true;
  if (fireWithRepeat(rightHeld, repR, now, 170, 90)) a.right = true;

  a.down = downHeld; // soft drop while held

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

  // Boot flash so you KNOW pixels are being written
  strip.fill(strip.Color(20, 0, 0)); strip.show(); delay(120);
  strip.fill(strip.Color(0, 20, 0)); strip.show(); delay(120);
  strip.fill(strip.Color(0, 0, 20)); strip.show(); delay(120);
  strip.clear(); strip.show();

  BP32.setup(&onConnected, &onDisconnected);
  BP32.enableNewBluetoothConnections(true);

  resetGame();

  Serial.println("\nTetris ready.");
  Serial.println("Xbox: D-pad move, A=rotate, B=drop, Y=restart.");
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
