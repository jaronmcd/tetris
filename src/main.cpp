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
static bool aiMode = true;                 // <-- START WITH AI ON
static constexpr uint32_t IDLE_TO_AI_MS = 8000;  // idle time to re-enable AI after human stops

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
  Serial.println("Xbox: D-pad move, A=rotate, B=drop, Y=restart.");
  Serial.println("Serial: a/d/w/s/space, r=restart");
  Serial.println("AI demo starts ON. Any input takes over instantly.");
}

void loop() {
  input.update();

  uint32_t now = millis();
  Actions human = input.readActions();

  // Any human input cancels AI immediately
  if (anyHumanAction(human)) {
    lastHumanMs = now;
    if (aiMode) {
      aiMode = false;
      ai.reset();
      Serial.println("AI demo off (human input).");
    }
  }

  // Re-enter AI after idle period (only if human has taken over before)
  if (!aiMode && (now - lastHumanMs) > IDLE_TO_AI_MS) {
    aiMode = true;
    ai.reset();
    Serial.println("AI demo on.");
  }

  Actions a = human;
  if (aiMode) {
    a = ai.think(game, now);
  }

  auto tr = game.tick(now, a);
  if (tr.levelUp) {
    display.levelUpFlash();
    Serial.printf("Level %u\n", game.level());
  }

  // Render ~30 FPS
  if ((now - lastFrameMs) >= 33) {
    lastFrameMs = now;
    display.render(game, now);
  }
}
