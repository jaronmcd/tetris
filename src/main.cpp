#include <Arduino.h>
#include "actions.h"
#include "tetris.h"
#include "display_matrix.h"
#include "input.h"
#include "ai.h"
#include "config.h"

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

// Boot skip state
static bool g_bootSkipped = false;

// Called frequently during boot screens. Returns true to abort/skip.
static bool bootAbort() {
  input.update();
  Actions a = input.readActions();
  if (anyHumanAction(a)) {
    g_bootSkipped = true;
    return true;
  }
  return false;
}

void setup() {
  Serial.begin(115200);
  delay(200);

  display.begin();
  display.bootFlash(); // RGB Flash

  input.begin();
  game.begin(); // Loads HS from memory

  // CONFIG TOGGLE TO WIPE DATA
  if (RESET_SCORES_ON_BOOT) {
    game.formatStorage(); // Wipe data
  }

  // Optional splash/logo (skippable with any input)
  display.showBootLogo(3500, &bootAbort);

  // Show boot stats (also skippable)
  if (!g_bootSkipped) {
    Serial.print("Booting... High Score: "); Serial.print(game.highScore());
    Serial.print(" | High Level: "); Serial.println(game.highLevel());
    display.showBootStats(game.highScore(), game.highLevel(), &bootAbort);
  }

  // Small flush so the "skip" button doesn't also immediately move/drop on first frame.
  for (int i = 0; i < 4; i++) {
    input.update();
    input.readActions();
    delay(20);
  }

  ai.reset();
  lastHumanMs = millis();

  Serial.println("\nTetris ready.");
  Serial.println("Serial debug keys:");
  Serial.println("  z/x/c/v = force 1/2/3/4-line clear FX (test mode)");
  Serial.println("  [ / ]   = level -1 / +1 (animates transition, test mode)");
  Serial.println("  { / }   = level -10 / +10 (animates transition, test mode)\n");
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
      Serial.println(">> Human Control Active");
      aiMode = false;
      ai.reset();
    }
  }

  if (!aiMode && (now - lastHumanMs) > IDLE_TO_AI_MS) {
    Serial.println(">> AI Demo Mode Active");
    aiMode = true;
    ai.reset();
  }

  Actions act = human;
  if (aiMode) {
    act = ai.think(game, now);
  }

  // Serial-only debug: step levels for testing colors/transitions.
  // This works in BOTH AI demo mode and human mode.
  if (human.testLevelDelta != 0) {
    uint8_t from = game.level();
    int target = (int)from + (int)human.testLevelDelta;
    if (target < 1) target = 1;
    if (target > 99) target = 99;

    if ((uint8_t)target != from) {
      Serial.print(">> DEBUG LEVEL: ");
      Serial.print(from);
      Serial.print(" -> ");
      Serial.println((uint8_t)target);

      game.debugSetLevel(now, (uint8_t)target);
      display.levelTransition(from, (uint8_t)target);
    }
  }


  // Always pass through serial-only debug commands, even in AI mode.
  act.testClearLines = human.testClearLines;
  act.testLevelDelta = human.testLevelDelta;

  // If human, always true. If AI, check the config.
  bool allowHighScore = (!aiMode) || (AI_SAVES_HIGH_SCORE);

  // Pass allowHighScore to tick
  TetrisGame::TickResult res = game.tick(now, act, allowHighScore);

  // --- LEVEL UP LOGIC ---
  if (res.levelUp) {
    Serial.println("\n=============================");
    Serial.print  ("   !!! LEVEL UP: "); Serial.print(game.level()); Serial.println(" !!!");
    Serial.print  ("   Score: ");        Serial.println(game.score());
    Serial.println("=============================\n");

    display.levelUpFlash(game.level());
  }

  // --- GAME OVER LOGIC ---
  if (res.gameOver) {
    Serial.println("GAME OVER");
    Serial.print("Final Score: "); Serial.println(game.score());

    if (res.newHighScore) {
      Serial.println(">>> NEW HIGH SCORE! <<<");
      display.showNewHighScore(game.score());
    } else {
      display.showGameOver(game.score(), game.level());
    }

    // Restart
    Serial.println("Restarting...");
    game.reset();
    display.bootFlash();
  }

  if (now - lastFrameMs >= 15) {
    lastFrameMs = now;
    display.render(game, now);
  }
}
