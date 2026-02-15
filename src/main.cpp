#include <Arduino.h>
#include <Preferences.h>
#include "actions.h"
#include "tetris.h"
#include "display_matrix.h"
#include "input.h"
#include "ai.h"
#include "config.h"
#include "breakout.h"

static TetrisGame game;
static MatrixDisplay display;
static InputManager input;
static TetrisAI ai;
static BreakoutAI breakoutAi;
static BreakoutGame breakout;
static Preferences bootModePrefs;

enum class GameMode : uint8_t {
  Menu = 0,
  Tetris = 1,
  Breakout = 2,
};

static GameMode g_mode = GameMode::Menu;
static uint8_t g_menuSelection = 0;
static uint8_t g_persistedBootMode = 0; // 0=tetris(default), 1=breakout

static uint32_t lastFrameMs = 0;

// Debug: force \"high score\" border mode (serial key: h)
static bool debugForceHighScoreBorders = false;

// Debug: preview MAX-level chase progress fill (serial key: g)
static uint16_t debugPreviewMaxChaseAttempts = 0;

// AI smartness ladder (auto-set from MAX chase cycles)
static uint8_t lastAutoAiSkill = 255;
static uint8_t lastAutoAiRampPct = 255;

#if AI_ADAPTIVE_EVOLUTION_ENABLED
static uint8_t g_adaptiveSignalPct = 0;
static bool g_adaptiveSkillBoostActive = false;

static uint8_t clampPctU8(uint16_t v) {
  if (v > 100u) v = 100u;
  return (uint8_t)v;
}

static uint8_t computeAdaptivePressurePct(const TetrisGame& game) {
  const uint8_t (*b)[BOARD_W] = game.board();
  int topRow = BOARD_H;
  for (int y = 0; y < BOARD_H; y++) {
    for (int x = 0; x < BOARD_W; x++) {
      if (b[y][x] != 0) {
        topRow = y;
        break;
      }
    }
    if (topRow != BOARD_H) break;
  }

  if (topRow == BOARD_H) return 0;

  int start = (int)AI_ADAPTIVE_PRESSURE_START_ROW;
  int full = (int)AI_ADAPTIVE_PRESSURE_FULL_ROW;
  if (start < 0) start = 0;
  if (full < 0) full = 0;
  if (start > (BOARD_H - 1)) start = BOARD_H - 1;
  if (full > (BOARD_H - 1)) full = BOARD_H - 1;

  if (start == full) return (topRow <= full) ? 100 : 0;

  // Normal case: smaller row index means closer to top and more danger.
  if (start > full) {
    if (topRow >= start) return 0;
    if (topRow <= full) return 100;
    uint16_t num = (uint16_t)(start - topRow);
    uint16_t den = (uint16_t)(start - full);
    return clampPctU8((uint16_t)((num * 100u) / den));
  }

  // Misconfiguration fallback (reversed rows).
  if (topRow <= start) return 0;
  if (topRow >= full) return 100;
  uint16_t num = (uint16_t)(topRow - start);
  uint16_t den = (uint16_t)(full - start);
  return clampPctU8((uint16_t)((num * 100u) / den));
}

static uint8_t computeAdaptiveMessPct(const TetrisGame& game) {
  const uint8_t (*b)[BOARD_W] = game.board();
  int heights[BOARD_W] = {};
  uint16_t holes = 0;

  for (int x = 0; x < BOARD_W; x++) {
    bool seen = false;
    int h = 0;
    for (int y = 0; y < BOARD_H; y++) {
      bool filled = (b[y][x] != 0);
      if (filled && !seen) {
        seen = true;
        h = BOARD_H - y;
      } else if (!filled && seen) {
        holes++;
      }
    }
    heights[x] = h;
  }

  uint16_t bump = 0;
  for (int x = 1; x < BOARD_W; x++) {
    int d = heights[x] - heights[x - 1];
    if (d < 0) d = -d;
    bump += (uint16_t)d;
  }

  const uint16_t holesMax = (uint16_t)(BOARD_W * BOARD_H);
  const uint16_t bumpMax = (BOARD_W > 1) ? (uint16_t)((BOARD_W - 1) * BOARD_H) : 1;
  uint8_t holesPct = clampPctU8((uint16_t)(((uint32_t)holes * 100u) / holesMax));
  uint8_t bumpPct = clampPctU8((uint16_t)(((uint32_t)bump * 100u) / bumpMax));

  // Holes are usually more harmful than bumpiness, so weight them higher.
  return (uint8_t)(((uint16_t)holesPct * 65u + (uint16_t)bumpPct * 35u) / 100u);
}

static void resetAdaptiveAiState() {
  g_adaptiveSignalPct = 0;
  g_adaptiveSkillBoostActive = false;
}
#endif

// Demo mode timing
static uint32_t lastHumanMs = 0;
static bool aiMode = true;
static bool paused = false;
static uint32_t pauseRenderNowMs = 0;
static uint32_t pauseResetHoldStartMs = 0;
static bool pauseResetHoldActive = false;
static bool breakoutPaused = false;
static uint32_t breakoutPauseRenderNowMs = 0;
static uint32_t breakoutPauseResetHoldStartMs = 0;
static bool breakoutPauseResetHoldActive = false;
static uint32_t breakoutLastHumanMs = 0;
static bool breakoutAiMode = false;
static constexpr uint32_t IDLE_TO_AI_MS = 8000;
static constexpr uint32_t PAUSE_RESET_HOLD_MS = 3000;

static inline bool anyHumanAction(const Actions& a) {
  return a.left || a.right || a.rotate || a.down || a.drop || a.togglePause || a.restart;
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

static inline bool menuConfirmAction(const Actions& a) {
  return a.rotate || a.drop || a.down || a.togglePause || a.restart;
}

static inline uint8_t normalizeBootMode(uint8_t v) {
  return (v == 1u) ? 1u : 0u;
}

static void loadBootModePref() {
  bootModePrefs.begin("arcade", true);
  g_persistedBootMode = normalizeBootMode((uint8_t)bootModePrefs.getUChar("bootm", 0));
  bootModePrefs.end();
  g_menuSelection = g_persistedBootMode;
}

static void saveBootModePref(uint8_t mode) {
  mode = normalizeBootMode(mode);
  g_persistedBootMode = mode;
  bootModePrefs.begin("arcade", false);
  bootModePrefs.putUChar("bootm", mode);
  bootModePrefs.end();
}

static void enterMenu(uint32_t nowMs) {
  g_mode = GameMode::Menu;
  g_menuSelection = g_persistedBootMode;

  paused = false;
  pauseResetHoldStartMs = 0;
  pauseResetHoldActive = false;

  breakoutPaused = false;
  breakoutPauseResetHoldStartMs = 0;
  breakoutPauseResetHoldActive = false;
  breakoutAiMode = false;

  display.setPaused(false, nowMs);
  game.debugResyncTimers(nowMs);
  lastFrameMs = nowMs;
}

static void startTetrisMode(uint32_t nowMs) {
  g_mode = GameMode::Tetris;
  paused = false;
  pauseResetHoldStartMs = 0;
  pauseResetHoldActive = false;

  display.setPaused(false, nowMs);

  game.reset();
  game.debugResyncTimers(nowMs);
  ai.reset();
  aiMode = false;
  lastHumanMs = nowMs;
  lastAutoAiSkill = 255;
  lastAutoAiRampPct = 255;
#if AI_ADAPTIVE_EVOLUTION_ENABLED
  resetAdaptiveAiState();
#endif
  lastFrameMs = nowMs;
}

static void startBreakoutMode(uint32_t nowMs) {
  g_mode = GameMode::Breakout;
  breakout.reset(nowMs);
  breakoutPaused = false;
  breakoutPauseResetHoldStartMs = 0;
  breakoutPauseResetHoldActive = false;
  breakoutAiMode = false;
  breakoutLastHumanMs = nowMs;
  breakoutAi.reset();
  display.setPaused(false, nowMs);
  lastFrameMs = nowMs;
}

static void launchSelectedMode(uint32_t nowMs) {
  saveBootModePref(g_menuSelection);

  if (g_menuSelection == 0) {
    Serial.println(">> Starting Tetris");
    startTetrisMode(nowMs);
    return;
  }

  Serial.println(">> Starting Breakout");
  startBreakoutMode(nowMs);
}

void setup() {
  Serial.begin(115200);
  delay(30);

  // Initialize input before boot screens so bootAbort() can safely poll buttons.
  input.begin();

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
  game.begin(); // Loads HS from memory
  breakout.begin(millis());

  // CONFIG TOGGLE TO WIPE DATA
  if (RESET_SCORES_ON_BOOT) {
    game.formatStorage(); // Wipe data
    breakout.formatStorage();
  }

  #if BOOT_STATS_ENABLED
  // Power-up: show the MAX-level "high screen" status (skippable).
  display.showBootStats(game.maxLevel(), game.maxLevelChaseAttempts(), &bootAbort);

  // Re-sync timers after the blocking boot screen so the first tick doesn't "fast-forward".
  game.debugResyncTimers(millis());
  #endif


  // Small flush so the "skip" button doesn't also immediately move/drop on first frame.
  for (int i = 0; i < 4; i++) {
    input.update();
    input.readActions();
    delay(20);
  }

  ai.reset();
  breakoutAi.reset();
  lastHumanMs = millis();
  loadBootModePref();

  Serial.println("\nArcade ready.");
  Serial.print("Boot mode: ");
  Serial.println((g_persistedBootMode == 0) ? "Tetris" : "Breakout");
  Serial.println("Hold SELECT/BACK/SHARE while paused to open the game menu.");
  Serial.println("Breakout controls:");
  Serial.println("  Left / Right = move paddle");
  Serial.println("  A / B / Down = launch ball");
  Serial.println("  START = pause/resume");
  Serial.println("  Idle ~8s = Breakout AI demo mode");
  Serial.println("  While paused: hold SELECT/BACK/SHARE ~3s to return to menu");
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

  if (g_persistedBootMode == 0) {
    startTetrisMode(lastHumanMs);
  } else {
    startBreakoutMode(lastHumanMs);
  }
}

void loop() {
  input.update();

  uint32_t now = millis();
  display.tickPowerBrightness(now);
  Actions human = input.readActions();

  if (human.aiProfileSet >= 0) {
    ai.setProfile((uint8_t)human.aiProfileSet);
    breakoutAi.setProfile((uint8_t)human.aiProfileSet);
    human.aiProfileSet = -1;
  }

  if (g_mode == GameMode::Menu) {
    if (human.left && g_menuSelection > 0) {
      g_menuSelection--;
    }
    if (human.right && g_menuSelection < 1) {
      g_menuSelection++;
    }

    if (menuConfirmAction(human)) {
      launchSelectedMode(now);
      return;
    }

    if ((now - lastFrameMs) >= 33) {
      lastFrameMs = now;
      display.renderGameSelectMenu(g_menuSelection, now);
    }
    return;
  }

  if (g_mode == GameMode::Breakout) {
    if (anyHumanAction(human)) {
      breakoutLastHumanMs = now;
      if (breakoutAiMode) {
        Serial.println(">> Breakout Human Control Active");
        breakoutAiMode = false;
        breakoutAi.reset();
      }
    }

    if (human.togglePause && !breakout.isGameOver()) {
      breakoutPaused = !breakoutPaused;
      display.setPaused(breakoutPaused, now);
      breakoutPauseResetHoldStartMs = 0;
      if (breakoutPaused) {
        breakoutPauseRenderNowMs = now;
        Serial.println(">> Breakout paused");
      } else {
        Serial.println(">> Breakout resumed");
      }
    }

    if (breakoutPaused) {
      uint32_t heldMs = 0;
      if (human.pauseResetHeld) {
        if (breakoutPauseResetHoldStartMs == 0) breakoutPauseResetHoldStartMs = now;
        if (!breakoutPauseResetHoldActive) {
          breakoutPauseResetHoldActive = true;
          Serial.println(">> Breakout hold reset started");
        }
        heldMs = now - breakoutPauseResetHoldStartMs;
      } else {
        if (breakoutPauseResetHoldActive) {
          breakoutPauseResetHoldActive = false;
          Serial.println(">> Breakout hold reset canceled");
        }
        breakoutPauseResetHoldStartMs = 0;
      }

      if (human.restart || heldMs >= PAUSE_RESET_HOLD_MS) {
        Serial.println(">> Returning to game menu");
        enterMenu(now);

        for (int i = 0; i < 3; i++) {
          input.update();
          input.readActions();
          delay(15);
        }
        return;
      }

      if (now - lastFrameMs >= 15) {
        lastFrameMs = now;
        display.renderBreakout(breakout, breakoutPauseRenderNowMs);
        if (breakoutPauseResetHoldStartMs != 0) {
          display.showPauseResetHoldFade(heldMs, PAUSE_RESET_HOLD_MS);
        }
      }
      return;
    }

    if (human.restart) {
      Serial.println(">> Breakout -> menu");
      enterMenu(now);
      return;
    }

    if (!breakoutAiMode && (now - breakoutLastHumanMs) > IDLE_TO_AI_MS) {
      Serial.println(">> Breakout AI Demo Mode Active");
      breakoutAiMode = true;
      breakoutAi.reset();
    }

    Actions act = human;
    if (breakoutAiMode) {
      act = breakoutAi.think(breakout, now);
    }

    if (breakout.isGameOver()) {
      if (act.rotate || act.drop || act.down) {
        Serial.println(">> Breakout restart");
        breakout.reset(now);
      }
    } else {
      bool breakoutAllowHighScore = (!breakoutAiMode) || (AI_SAVES_HIGH_SCORE);
      BreakoutGame::TickResult br = breakout.tick(now, act, breakoutAllowHighScore);
      if (br.lostLife) {
        Serial.print(">> Breakout life lost. Lives left: ");
        Serial.println(breakout.lives());
      }
      if (br.gameOver) {
        if (br.won) {
          Serial.println(">> Breakout: you cleared every brick!");
        } else {
          Serial.println(">> Breakout: game over");
        }
        Serial.print(">> Breakout score: ");
        Serial.println(breakout.score());
        Serial.print(">> Breakout high score: ");
        Serial.println(breakout.highScore());
        if (br.newHighScore) {
          Serial.println(">>> NEW BREAKOUT HIGH SCORE <<<");
        }
      }
    }

    if (now - lastFrameMs >= 15) {
      lastFrameMs = now;
      display.renderBreakout(breakout, now);
    }
    return;
  }

  if (human.togglePause) {
    paused = !paused;
    display.setPaused(paused, now);
    pauseResetHoldStartMs = 0;
    if (paused) {
      pauseRenderNowMs = now; // freeze render-time animations while paused
      Serial.println(">> Paused");
      if (aiMode) {
        aiMode = false;
        ai.reset();
      }
    } else {
      Serial.println(">> Resumed");
      game.debugResyncTimers(now);
      lastHumanMs = now;
    }
  }

  if (paused) {
    uint32_t heldMs = 0;
    if (human.pauseResetHeld) {
      if (pauseResetHoldStartMs == 0) pauseResetHoldStartMs = now;
      if (!pauseResetHoldActive) {
        pauseResetHoldActive = true;
        Serial.println(">> Hold reset started");
      }
      heldMs = now - pauseResetHoldStartMs;
    } else {
      if (pauseResetHoldActive) {
        pauseResetHoldActive = false;
        Serial.println(">> Hold reset canceled");
      }
      pauseResetHoldStartMs = 0;
    }

    // Serial restart remains as a quick return-to-menu shortcut.
    if (human.restart || heldMs >= PAUSE_RESET_HOLD_MS) {
      if (human.restart) {
        Serial.println(">> Returning to menu (serial)...");
      } else {
        Serial.println(">> Pause reset hold complete. Returning to menu...");
      }

      enterMenu(now);

      for (int i = 0; i < 3; i++) {
        input.update();
        input.readActions();
        delay(15);
      }
      return;
    }

    if (now - lastFrameMs >= 15) {
      lastFrameMs = now;
      display.render(game, pauseRenderNowMs);
      if (pauseResetHoldStartMs != 0) {
        display.showPauseResetHoldFade(heldMs, PAUSE_RESET_HOLD_MS);
      }
    }
    return;
  }


  // Auto-tune AI "smartness" based on MAX chase attempt cycles.
  // This is separate from the AI speed profile (0..3). The profile controls
  // how fast the AI presses buttons; the smartness ladder controls how good
  // its placements are.
  uint8_t targetSkill = (uint8_t)AI_SMARTNESS_BASE;
  uint8_t targetRampPct = 0;
  uint32_t chaseCycle = 0;
#if AI_ADAPTIVE_EVOLUTION_ENABLED
  bool adaptiveBoostApplied = false;
#endif
#if AI_SMARTNESS_FROM_MAX_CHASE_ENABLED && MAX_LEVEL_CHASE_PROGRESS_ENABLED
  uint16_t steps = (uint16_t)MAX_LEVEL_CHASE_PROGRESS_STEPS;
  if (steps == 0) steps = 1;
  uint16_t attempts = game.maxLevelChaseAttempts();
  chaseCycle = (uint32_t)(attempts / steps);
  uint16_t inCycle = (uint16_t)(attempts % steps);
  uint32_t s = (uint32_t)AI_SMARTNESS_BASE + chaseCycle;
  if (s > (uint32_t)AI_SMARTNESS_MAX) s = (uint32_t)AI_SMARTNESS_MAX;
  targetSkill = (uint8_t)s;

  // Incremental "learning": as the MAX chase progress bar fills (0..steps-1),
  // ramp toward the next skill tier. Major milestones remain unchanged (skill
  // still only increases on a full fill cycle).
  if (targetSkill < (uint8_t)AI_SMARTNESS_MAX) {
    targetRampPct = (uint8_t)(((uint32_t)inCycle * 100u) / (uint32_t)steps);
  } else {
    targetRampPct = 0;
  }
#endif

#if AI_ADAPTIVE_EVOLUTION_ENABLED
  {
    uint8_t pressurePct = computeAdaptivePressurePct(game);
    uint8_t messPct = computeAdaptiveMessPct(game);

    // Pressure dominates; messiness is a secondary cue.
    uint8_t rawAdaptivePct = (uint8_t)(((uint16_t)pressurePct * 80u + (uint16_t)messPct * 20u) / 100u);
    g_adaptiveSignalPct = (uint8_t)(((uint16_t)g_adaptiveSignalPct * 7u + (uint16_t)rawAdaptivePct * 3u) / 10u);

    uint8_t onPct = clampPctU8((uint16_t)AI_ADAPTIVE_SKILL_BOOST_ON_PCT);
    uint8_t offPct = clampPctU8((uint16_t)AI_ADAPTIVE_SKILL_BOOST_OFF_PCT);
    if (offPct > onPct) offPct = onPct;

    bool prevBoost = g_adaptiveSkillBoostActive;
    if (!g_adaptiveSkillBoostActive && g_adaptiveSignalPct >= onPct) {
      g_adaptiveSkillBoostActive = true;
    } else if (g_adaptiveSkillBoostActive && g_adaptiveSignalPct <= offPct) {
      g_adaptiveSkillBoostActive = false;
    }

    if (prevBoost != g_adaptiveSkillBoostActive) {
      Serial.print(">> AI adaptive boost ");
      Serial.print(g_adaptiveSkillBoostActive ? "ON" : "OFF");
      Serial.print(" (signal=");
      Serial.print(g_adaptiveSignalPct);
      Serial.println("%)");
    }

    if (targetSkill < (uint8_t)AI_SMARTNESS_MAX) {
      uint8_t bonusMaxPct = clampPctU8((uint16_t)AI_ADAPTIVE_RAMP_BONUS_MAX_PCT);
      uint8_t bonusPct = (uint8_t)(((uint16_t)g_adaptiveSignalPct * (uint16_t)bonusMaxPct) / 100u);
      uint16_t rampPlus = (uint16_t)targetRampPct + (uint16_t)bonusPct;
      targetRampPct = clampPctU8(rampPlus);
    } else {
      targetRampPct = 0;
    }

    if (g_adaptiveSkillBoostActive && targetSkill < (uint8_t)AI_SMARTNESS_MAX) {
      targetSkill = (uint8_t)(targetSkill + 1u);
      adaptiveBoostApplied = true;
      if (targetSkill >= (uint8_t)AI_SMARTNESS_MAX) targetRampPct = 0;
    }
  }
#endif
  if (targetSkill != lastAutoAiSkill) {
    lastAutoAiSkill = targetSkill;
    ai.setSkill(targetSkill);
    Serial.print(">> AI smartness level = ");
    Serial.print(targetSkill);
#if AI_SMARTNESS_FROM_MAX_CHASE_ENABLED && MAX_LEVEL_CHASE_PROGRESS_ENABLED
    Serial.print(" (max-chase cycle=");
    Serial.print(chaseCycle);
#if AI_ADAPTIVE_EVOLUTION_ENABLED
    if (adaptiveBoostApplied) Serial.print(", adaptive+1");
#endif
    Serial.println(")");
#else
#if AI_ADAPTIVE_EVOLUTION_ENABLED
    if (adaptiveBoostApplied) Serial.print(" (adaptive+1)");
#endif
    Serial.println();
#endif
  }

  if (targetRampPct != lastAutoAiRampPct) {
    lastAutoAiRampPct = targetRampPct;
    ai.setSkillRampPct(targetRampPct);
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
#if AI_ADAPTIVE_EVOLUTION_ENABLED
    resetAdaptiveAiState();
#endif
    display.bootFlash();
  }

  if (now - lastFrameMs >= 15) {
    lastFrameMs = now;
    display.render(game, now);
  }
}
