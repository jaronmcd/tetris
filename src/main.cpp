#include <Arduino.h>
#include "actions.h"
#include "tetris.h"
#include "display_matrix.h"
#include "input.h"
#include "ai.h"

static TetrisGame game;
static MatrixDisplay display;
static InputManager input;
static TetrisAI ai;

static uint32_t lastFrameMs = 0;

// Demo mode timing
static uint32_t lastHumanMs = 0;
static bool aiMode = true;                 
static constexpr uint32_t IDLE_TO_AI_MS = 8000;  

static inline bool anyHumanAction(const Actions& a) {
  return a.left || a.right || a.rotate || a.down || a.drop || a.restart;
}

void setup() {
  Serial.begin(115200);
  delay(200);

  display.begin();
  display.bootFlash();

  input.begin();
  game.begin();

  ai.reset();
  lastHumanMs = millis();

  Serial.println("\nTetris ready.");
}

void loop() {
  input.update();

  uint32_t now = millis();
  Actions human = input.readActions();

  if (human.aiProfileSet >= 0) {
    ai.setProfile((uint8_t)human.aiProfileSet);
    human.aiProfileSet = -1;
  }

  if (anyHumanAction(human)) {
    lastHumanMs = now;
    if (aiMode) {
      aiMode = false;
      ai.reset();
    }
  }

  if (!aiMode && (now - lastHumanMs) > IDLE_TO_AI_MS) {
    aiMode = true;
    ai.reset();
  }

  Actions act = human;
  if (aiMode) {
    act = ai.think(game, now);
  }

  TetrisGame::TickResult res = game.tick(now, act);

  // --- UPDATED: Pass the level to the flash function ---
  if (res.levelUp) {
    display.levelUpFlash(game.level());
  }

  if (now - lastFrameMs >= 15) {
    lastFrameMs = now;
    display.render(game, now);
  }
}