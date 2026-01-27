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

// Debug: force \"high score\" border mode (serial key: h)
static bool debugForceHighScoreBorders = false;

// Debug: preview MAX-level chase progress fill (serial key: g)
static uint16_t debugPreviewMaxChaseAttempts = 0;

// AI smartness ladder (auto-set from MAX chase cycles)
static uint8_t lastAutoAiSkill = 255;

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
  delay(30);

  display.begin();

  // Boot title: big scrolling text (skippable)
  #if INTRO_ENABLED && INTRO_MARQUEE_ENABLED
  display.showIntroMarquee(INTRO_MARQUEE_TEXT, (uint8_t)INTRO_MARQUEE_SCALE, (uint32_t)INTRO_MARQUEE_MAX_MS, &bootAbort);
  #endif

  // Boot intro: rapid falling pieces that fill the matrix (skippable)
  #if INTRO_ENABLED
  display.showIntroDropFill((uint32_t)INTRO_MAX_MS, &bootAbort);
  #endif

display.bootFlash(); // RGB Flash

  input.begin();
  game.begin(); // Loads HS from memory

  // CONFIG TOGGLE TO WIPE DATA
  if (RESET_SCORES_ON_BOOT) {
    game.formatStorage(); // Wipe data
  }

  // Boot: keep it minimal on the tiny display (MAX level only). Skippable.

  // Show boot stats (also skippable)// Small flush so the "skip" button doesn't also immediately move/drop on first frame.
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
  Serial.println("  { / }   = level -10 / +10 (animates transition, test mode)");
  Serial.println("  h       = toggle high-score border style (test mode)");
  Serial.println("  b       = preview boot stats (MAX level screen)");
  Serial.println("  o       = preview game over (live: current+max)");
  Serial.println("  p       = preview game over (forced non-record)");
  Serial.println("  t       = preview game over (forced tie/record)");
  Serial.println("  m       = preview new MAX level celebration");
  Serial.println("  g       = preview MAX chase progress (cycles fill/colors)");
  Serial.println();
}

void loop() {
  input.update();

  uint32_t now = millis();
  display.tickPowerBrightness(now);
  Actions human = input.readActions();

  if (human.aiProfileSet >= 0) {
    ai.setProfile((uint8_t)human.aiProfileSet);
    human.aiProfileSet = -1;
  }


  // Auto-tune AI "smartness" based on MAX chase attempt cycles.
  // This is separate from the AI speed profile (0..3). The profile controls
  // how fast the AI presses buttons; the smartness ladder controls how good
  // its placements are.
  uint8_t targetSkill = (uint8_t)AI_SMARTNESS_BASE;
  uint32_t chaseCycle = 0;
#if AI_SMARTNESS_FROM_MAX_CHASE_ENABLED && MAX_LEVEL_CHASE_PROGRESS_ENABLED
  uint16_t steps = (uint16_t)MAX_LEVEL_CHASE_PROGRESS_STEPS;
  if (steps == 0) steps = 1;
  uint16_t attempts = game.maxLevelChaseAttempts();
  chaseCycle = (uint32_t)(attempts / steps);
  uint32_t s = (uint32_t)AI_SMARTNESS_BASE + chaseCycle;
  if (s > (uint32_t)AI_SMARTNESS_MAX) s = (uint32_t)AI_SMARTNESS_MAX;
  targetSkill = (uint8_t)s;
#endif
  if (targetSkill != lastAutoAiSkill) {
    lastAutoAiSkill = targetSkill;
    ai.setSkill(targetSkill);
    Serial.print(">> AI smartness level = ");
    Serial.print(targetSkill);
#if AI_SMARTNESS_FROM_MAX_CHASE_ENABLED && MAX_LEVEL_CHASE_PROGRESS_ENABLED
    Serial.print(" (max-chase cycle=");
    Serial.print(chaseCycle);
    Serial.println(")");
#else
    Serial.println();
#endif
  }

  if (human.toggleHighScoreBorders) {
    debugForceHighScoreBorders = !debugForceHighScoreBorders;
    display.setDebugForceHighScoreBorders(debugForceHighScoreBorders);
    Serial.print(">> DEBUG: High-score border ");
    Serial.println(debugForceHighScoreBorders ? "ON" : "OFF");
  }

  // Serial-only debug: preview the high-score / end-of-run screens without
  // having to play a full game. These screens are blocking (they use delays),
  // so after they return we re-sync the game's timing.
  if (human.previewScreen != 0) {
    uint8_t ml = game.maxLevel();
    uint8_t lvl = game.level();

    switch (human.previewScreen) {
      case 1: {
        Serial.print(">> DEBUG PREVIEW: Boot stats (MAX level = ");
        Serial.print(ml);
        Serial.println(")");
        display.showBootStats(ml, game.maxLevelChaseAttempts(), nullptr);
      } break;

      case 2: {
        Serial.print(">> DEBUG PREVIEW: Game over (level=");
        Serial.print(lvl);
        Serial.print(", max=");
        Serial.print(ml);
        Serial.println(")");
        display.showGameOver(lvl, ml, game.maxLevelChaseAttempts());
      } break;

      case 3: {
        // Force the two-screen version even if the current run is a tie/record.
        uint8_t maxShown = (ml < 2) ? 2 : ml;
        uint8_t levelShown = (maxShown > 1) ? (uint8_t)(maxShown - 1) : 1;
        Serial.print(">> DEBUG PREVIEW: Game over (forced non-record, level=");
        Serial.print(levelShown);
        Serial.print(", max=");
        Serial.print(maxShown);
        Serial.println(")");
        display.showGameOver(levelShown, maxShown, game.maxLevelChaseAttempts());
      } break;

      case 4: {
        Serial.print(">> DEBUG PREVIEW: Game over (forced tie/record, level=max=");
        Serial.print(ml);
        Serial.println(")");
        display.showGameOver(ml, ml, game.maxLevelChaseAttempts());
      } break;

      case 5: {
        uint8_t show = (ml < 99) ? (uint8_t)(ml + 1) : ml;
        Serial.print(">> DEBUG PREVIEW: New MAX level celebration (");
        Serial.print(show);
        Serial.println(")");
        display.showNewMaxLevel(show);
      } break;

      case 6: {
        uint16_t steps = (uint16_t)MAX_LEVEL_CHASE_PROGRESS_STEPS;
        if (steps < 1) steps = 1;

        // Cycle through multiple full fills so you can preview the "infinite color" behavior.
        // Range: 0 .. (steps * PREVIEW_CYCLES), inclusive.
        const uint32_t PREVIEW_CYCLES = 6u;
        uint32_t range = (uint32_t)steps * PREVIEW_CYCLES;
        if (range > 65535u) range = (uint32_t)steps; // safety (debug var is uint16_t)

        debugPreviewMaxChaseAttempts = (uint16_t)((debugPreviewMaxChaseAttempts + 1u) % (range + 1u));

        uint32_t cycle = (uint32_t)debugPreviewMaxChaseAttempts / (uint32_t)steps;
        uint16_t inCycle = (uint16_t)(debugPreviewMaxChaseAttempts % steps);

        Serial.print(">> DEBUG PREVIEW: MAX chase progress attempts=");
        Serial.print(debugPreviewMaxChaseAttempts);
        Serial.print("  (cycle=");
        Serial.print(cycle);
        Serial.print(", inCycle=");
        Serial.print(inCycle);
        Serial.print("/");
        Serial.print(steps);
        Serial.println(")");

        display.showBootStats(game.maxLevel(), debugPreviewMaxChaseAttempts, nullptr);
      } break;
    }

    // Treat this as a "human activity" so demo-mode doesn't unexpectedly flip.
    // (But do not force-exit AI mode.)
    uint32_t afterMs = millis();
    lastHumanMs = afterMs;
    now = afterMs; // keep the rest of the loop consistent after blocking delays

    // Re-sync timers after blocking screens so the piece doesn't instantly drop.
    game.debugResyncTimers(afterMs);
    display.tickPowerBrightness(afterMs);

    // Small input flush so the preview key doesn't accidentally also act as a control.
    for (int i = 0; i < 3; i++) {
      input.update();
      input.readActions();
      delay(15);
    }
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

    if (res.newMaxLevel) {
      Serial.print(">>> NEW MAX LEVEL: "); Serial.print(game.maxLevel()); Serial.println(" <<<");
      display.showNewMaxLevel(game.maxLevel());
    }

    display.showGameOver(game.level(), game.maxLevel(), game.maxLevelChaseAttempts());

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
