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

  // Show stats (will be 0 if wiped above)
  Serial.print("Booting... High Score: "); Serial.print(game.highScore());
  Serial.print(" | High Level: "); Serial.println(game.highLevel());
  
  display.showBootStats(game.highScore(), game.highLevel());

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

  // --- CHANGED: DETERMINE IF HIGH SCORE IS ALLOWED ---
  // If human, always true.
  // If AI, check the config.
  bool allowHighScore = (!aiMode) || (AI_SAVES_HIGH_SCORE);

  // Pass 'allowHighScore' to tick (THIS WAS THE MISSING PART)
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
    
    // NOTE: 'newHighScore' will only be true if allowHighScore was true AND they beat the score
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