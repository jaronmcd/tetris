#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include "config.h"
#include "esp_system.h"  // for esp_random()

// ---------- NeoPixel ----------
Adafruit_NeoPixel strip(MATRIX_W * MATRIX_H, LED_PIN, NEO_PIXEL_TYPE);

// Map (x,y) to NeoPixel index, supporting serpentine panels and optional bottom-up wiring.
static inline uint16_t XY(uint8_t x, uint8_t y) {
  if (x >= MATRIX_W || y >= MATRIX_H) return 0;

  uint8_t yy = MATRIX_BOTTOM_UP ? (MATRIX_H - 1 - y) : y;

  if (!SERPENTINE) {
    return (uint16_t)yy * MATRIX_W + x;
  }

  // serpentine rows: even rows L->R, odd rows R->L
  if ((yy & 1) == 0) {
    return (uint16_t)yy * MATRIX_W + x;
  } else {
    return (uint16_t)yy * MATRIX_W + (MATRIX_W - 1 - x);
  }
}

static inline void setPixel(uint8_t x, uint8_t y, uint32_t c) {
  strip.setPixelColor(XY(x, y), c);
}

// ---------- Input ----------
struct Actions {
  bool left = false;
  bool right = false;
  bool rotate = false;
  bool down = false;
  bool drop = false;
  bool restart = false;
};

struct Button {
  int pin = -1;
  bool lastStable = true;     // pullup => HIGH when not pressed
  bool lastRead = true;
  uint32_t lastChangeMs = 0;

  void begin(int p) {
    pin = p;
    if (pin >= 0) {
      pinMode(pin, INPUT_PULLUP);
      lastStable = digitalRead(pin);
      lastRead = lastStable;
      lastChangeMs = millis();
    }
  }

  // returns true on "pressed edge"
  bool pressedEdge(uint32_t nowMs, uint32_t debounceMs = 25) {
    if (pin < 0) return false;
    bool r = digitalRead(pin);

    if (r != lastRead) {
      lastRead = r;
      lastChangeMs = nowMs;
    }

    if ((nowMs - lastChangeMs) >= debounceMs && lastStable != lastRead) {
      bool prev = lastStable;
      lastStable = lastRead;
      // pressed edge: HIGH->LOW (active low)
      return (prev == true && lastStable == false);
    }
    return false;
  }

  bool isHeld() const {
    if (pin < 0) return false;
    return lastStable == false; // active low
  }
};

Button bLeft, bRight, bRotate, bDown, bDrop;

// ---------- Tetris ----------
static uint8_t board[BOARD_H][BOARD_W]; // 0 empty, 1..7 piece id

enum PieceType : uint8_t { I=0, O, T, S, Z, J, L };

struct Piece {
  uint8_t type = 0;  // 0..6
  int8_t x = 3;      // board coords (top-left of 4x4)
  int8_t y = -1;     // can start above board
  uint8_t rot = 0;   // 0..3
};

static Piece cur;
static bool gameOver = false;

static uint32_t score = 0;
static uint16_t linesCleared = 0;
static uint8_t level = 1;

static uint32_t lastFallMs = 0;
static uint32_t lastFrameMs = 0;

// 4x4 bitmasks, bit (r*4+c) from MSB? We'll use a simple "bit index" scheme:
// bit 15 is (0,0), bit 12 is (0,3), bit 0 is (3,3).
static inline bool maskCell(uint16_t m, uint8_t r, uint8_t c) {
  uint8_t bit = 15 - (r * 4 + c);
  return (m >> bit) & 1;
}

// Piece rotation masks (4 rotations each). These are standard 4x4 tetris masks.
static const uint16_t SHAPES[7][4] = {
  // I
  { 0x0F00, 0x2222, 0x00F0, 0x4444 },
  // O
  { 0x6600, 0x6600, 0x6600, 0x6600 },
  // T
  { 0x0E40, 0x4C40, 0x4E00, 0x4640 },
  // S
  { 0x06C0, 0x8C40, 0x06C0, 0x8C40 },
  // Z
  { 0x0C60, 0x4C80, 0x0C60, 0x4C80 },
  // J
  { 0x8E00, 0x6440, 0x0E20, 0x44C0 },
  // L
  { 0x2E00, 0x4460, 0x0E80, 0xC440 }
};

static const uint32_t PIECE_COLORS[8] = {
  0, // empty
  0x00FFFF, // I (cyan)
  0xFFFF00, // O (yellow)
  0xAA00FF, // T (purple)
  0x00FF00, // S (green)
  0xFF0000, // Z (red)
  0x0000FF, // J (blue)
  0xFF7F00  // L (orange)
};

// Convert 0xRRGGBB to NeoPixel Color()
static inline uint32_t rgb(uint32_t rrggbb) {
  uint8_t r = (rrggbb >> 16) & 0xFF;
  uint8_t g = (rrggbb >> 8) & 0xFF;
  uint8_t b = (rrggbb) & 0xFF;
  return strip.Color(r, g, b);
}

static bool fits(uint8_t type, uint8_t rot, int8_t px, int8_t py) {
  uint16_t m = SHAPES[type][rot & 3];
  for (uint8_t r = 0; r < 4; r++) {
    for (uint8_t c = 0; c < 4; c++) {
      if (!maskCell(m, r, c)) continue;

      int8_t bx = px + (int8_t)c;
      int8_t by = py + (int8_t)r;

      // Left/right bounds
      if (bx < 0 || bx >= BOARD_W) return false;

      // Bottom bound
      if (by >= BOARD_H) return false;

      // Above top is allowed during spawn
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
      if (by < 0) continue; // ignore above-top cells
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
  // Fisher-Yates shuffle
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

static void resetGame() {
  memset(board, 0, sizeof(board));
  score = 0;
  linesCleared = 0;
  level = 1;
  gameOver = false;

  bagIdx = 7;
  refillBag();

  cur.type = nextPieceType();
  cur.rot = 0;
  cur.x = 3;
  cur.y = -1;

  lastFallMs = millis();
}

static void updateLevel() {
  // Simple leveling: +1 level every 10 lines
  level = (linesCleared / 10) + 1;
}

static uint32_t dropIntervalMs() {
  // Faster as level increases; clamp to keep playable
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
      // pull everything above down by 1
      for (int yy = y; yy > 0; yy--) {
        memcpy(board[yy], board[yy - 1], BOARD_W);
      }
      memset(board[0], 0, BOARD_W);
      y++; // recheck same row index after shifting
    }
  }

  if (clearedThis > 0) {
    linesCleared += clearedThis;
    updateLevel();

    // Classic-ish scoring
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

  if (!fits(cur.type, cur.rot, cur.x, cur.y)) {
    gameOver = true;
  }
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
    // couldn't fall => lock
    lockAndContinue();
  }
}

static void tryRotate() {
  uint8_t nr = (cur.rot + 1) & 3;
  // basic wall kick: try center, then left, then right
  if (fits(cur.type, nr, cur.x, cur.y)) {
    cur.rot = nr;
    return;
  }
  if (fits(cur.type, nr, cur.x - 1, cur.y)) {
    cur.x -= 1; cur.rot = nr; return;
  }
  if (fits(cur.type, nr, cur.x + 1, cur.y)) {
    cur.x += 1; cur.rot = nr; return;
  }
}

static void hardDrop() {
  while (fits(cur.type, cur.rot, cur.x, cur.y + 1)) {
    cur.y += 1;
    score += 1; // small reward per dropped row
  }
  lockAndContinue();
}

// ---------- Rendering ----------
static void render() {
  strip.clear();

  // subtle border/background
  uint32_t border = strip.Color(6, 6, 6);

  // draw side margins as border columns
  for (uint8_t y = 0; y < MATRIX_H; y++) {
    for (uint8_t x = 0; x < MATRIX_W; x++) {
      bool inBoardX = (x >= BOARD_OFFSET_X) && (x < (BOARD_OFFSET_X + BOARD_W));
      bool inBoardY = (y >= BOARD_OFFSET_Y) && (y < (BOARD_OFFSET_Y + BOARD_H));
      if (!inBoardX || !inBoardY) {
        setPixel(x, y, border);
      }
    }
  }

  // board cells
  for (uint8_t by = 0; by < BOARD_H; by++) {
    for (uint8_t bx = 0; bx < BOARD_W; bx++) {
      uint8_t id = board[by][bx];
      if (id != 0) {
        uint8_t px = BOARD_OFFSET_X + bx;
        uint8_t py = BOARD_OFFSET_Y + by;
        setPixel(px, py, rgb(PIECE_COLORS[id]));
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

        uint8_t px = BOARD_OFFSET_X + (uint8_t)bx;
        uint8_t py = BOARD_OFFSET_Y + (uint8_t)by;
        setPixel(px, py, rgb(PIECE_COLORS[id]));
      }
    }
  } else {
    // game over: blink red
    bool on = ((millis() / 350) & 1) == 0;
    uint32_t c = on ? strip.Color(40, 0, 0) : strip.Color(0, 0, 0);
    for (uint8_t y = 0; y < MATRIX_H; y++) {
      for (uint8_t x = 0; x < MATRIX_W; x++) {
        setPixel(x, y, c);
      }
    }
  }

  strip.show();
}

// ---------- Input gather ----------
static Actions readActions() {
  Actions a;
  uint32_t now = millis();

  // Buttons (edge-triggered for most actions)
  a.left   |= bLeft.pressedEdge(now);
  a.right  |= bRight.pressedEdge(now);
  a.rotate |= bRotate.pressedEdge(now);
  a.drop   |= bDrop.pressedEdge(now);

  // Down can be held for soft drop
  if (BTN_DOWN_PIN >= 0) {
    // update stable state via edge call side-effects:
    bDown.pressedEdge(now);
    a.down = bDown.isHeld();
  }

  // Serial controls
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

// ---------- Setup / Loop ----------
void setup() {
  // Serial (native USB CDC if enabled by build_flags)
  Serial.begin(115200);
  delay(200);

  strip.begin();
  strip.setBrightness(BRIGHTNESS);
  strip.show();

  // Buttons
  bLeft.begin(BTN_LEFT_PIN);
  bRight.begin(BTN_RIGHT_PIN);
  bRotate.begin(BTN_ROTATE_PIN);
  bDown.begin(BTN_DOWN_PIN);
  bDrop.begin(BTN_DROP_PIN);

  // Seed randomness
  randomSeed((uint32_t)esp_random());

  resetGame();
  Serial.println("\nTetris 16x16 ready. Keys: a/d/w/s/space, r=restart");
}

void loop() {
  uint32_t now = millis();

  Actions act = readActions();

  if (act.restart) resetGame();

  if (!gameOver) {
    // lateral/rotate/drop actions
    if (act.left)  tryMove(-1, 0);
    if (act.right) tryMove(+1, 0);
    if (act.rotate) tryRotate();

    if (act.drop) {
      hardDrop();
    } else {
      // soft drop when held/pressed
      if (act.down) {
        // more frequent falls while down is held
        if ((now - lastFallMs) >= 60) {
          lastFallMs = now;
          tryMove(0, +1);
        }
      } else {
        // gravity fall
        if ((now - lastFallMs) >= dropIntervalMs()) {
          lastFallMs = now;
          tryMove(0, +1);
        }
      }
    }
  }

  // render at ~30fps
  if ((now - lastFrameMs) >= 33) {
    lastFrameMs = now;
    render();
  }
}
