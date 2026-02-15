#pragma once
#include <Arduino.h>
#include <stdint.h>
#include <Adafruit_NeoPixel.h>

#include "config.h"
#include "tetris.h"

class BreakoutGame;

class MatrixDisplay {
public:
  using AbortFn = bool (*)();

  MatrixDisplay();

  void begin();
  void bootFlash();
  void showTvPowerOff();
  void setPaused(bool paused, uint32_t nowMs);
  void showPauseResetHoldFade(uint32_t heldMs, uint32_t holdMsRequired);
  void renderSettingsScreen(uint32_t nowMs);

  uint8_t userBrightness() const { return userBrightness_; }
  void adjustUserBrightness(int8_t delta);

  uint8_t displayRotation() const { return rotationQuarterTurns_; }
  void rotateDisplay(int8_t quarterTurnsDelta);

  // Call frequently (e.g., once per loop). Handles brightness targeting + fades.
  void tickPowerBrightness(uint32_t nowMs);

  // Boot intro: rapid falling pieces that fill the screen (skippable).
  void showIntroDropFill(uint32_t maxDurationMs, AbortFn abortFn = nullptr);

  // Boot intro title: scrolling big text (e.g., "TETRIS"). Skippable.
  void showIntroMarquee(const char* text, uint8_t scale, uint32_t maxDurationMs, AbortFn abortFn = nullptr);

  // Boot intro hybrid: marquee titles + breakout-vs-tetris smash scene. Skippable.
  void showIntroHybridArcade(uint32_t maxDurationMs, AbortFn abortFn = nullptr);



  // Debug: force the "high score" border style regardless of score.
  void setDebugForceHighScoreBorders(bool enable);
  void levelUpFlash(uint8_t nextLevel);

  // Debug / test helper: animate the palette/border transition between ANY two levels.
  // (Used by serial level-stepping keys.)
  void levelTransition(uint8_t fromLevel, uint8_t toLevel);

  // Optional: Boot splash/logo screen (skippable)
  void showBootLogo(uint32_t durationMs, AbortFn abortFn = nullptr);

  // Normal Game Over (Score + Level)
  void showGameOver(uint8_t level, uint8_t maxLevel, uint16_t maxChaseAttempts);
// Celebration for beating the max-level record
  void showNewMaxLevel(uint8_t maxLevel);

  // Boot: shows MAX level (skippable)
  void showBootStats(uint8_t maxLevel, uint16_t maxChaseAttempts, AbortFn abortFn = nullptr);

  void render(const TetrisGame& g, uint32_t nowMs);
  void renderGameSelectMenu(uint8_t selectedGame, uint32_t nowMs);
  void renderBreakout(const BreakoutGame& g, uint32_t nowMs);

private:
  uint16_t XY(uint8_t x, uint8_t y) const;

  // int16 so we can draw off-screen (for scrolling)
  void setPixel(int16_t x, int16_t y, uint32_t c);

  uint32_t rgb(uint32_t rrggbb) const;

  uint8_t themeIndex(uint8_t level) const;
  uint32_t pieceColor(uint8_t level, uint8_t pieceId) const;
  uint32_t scaleColor(uint32_t c, uint8_t alpha) const;

  uint32_t solidBorderForLevel(uint8_t level, uint8_t x, uint8_t y, uint32_t nowMs) const;

  void drawChar(int16_t x, int16_t y, char c, uint32_t color);
  void drawTextCentered(String text, int16_t y, uint32_t color);
  void drawText(int16_t x, int16_t y, const String& text, uint32_t color);

// Minimal numeric screens (no labels / no score)
uint32_t dimColor(uint32_t c, uint8_t alpha) const;
void drawDigit5x7Scaled(uint8_t digit, int16_t x, int16_t y, uint8_t scale, uint32_t color);
void drawDigit5x7ScaledHalo(uint8_t digit, int16_t x, int16_t y, uint8_t scale,
                            uint32_t color, uint8_t haloAlpha, bool darken, uint8_t* haloMask);
void drawNumberCentered(uint8_t value, uint8_t scale, uint32_t color);
void drawNumberCenteredHalo(uint8_t value, uint8_t scale, uint32_t color, uint8_t haloAlpha, bool darken);
bool showLevelNumberScreen(uint8_t value, uint32_t bg, uint32_t fg, uint32_t durationMs, AbortFn abortFn);

// MAX-level screens can optionally show a "record chase" progress meter in the
// background based on the persistent attempt counter.
void fillMaxChaseBackground(uint32_t baseBg, uint32_t chaseBg, uint16_t maxChaseAttempts, uint32_t nowMs);
bool showMaxLevelNumberScreen(uint8_t value,
                              uint32_t baseBg, uint32_t chaseBg,
                              uint16_t maxChaseAttempts,
                              uint32_t fg,
                              uint32_t durationMs,
                              AbortFn abortFn);

  // Two-line screen: title on top, value on bottom (scrolls if needed). Returns true if aborted.
  bool showTwoLineTitleValue(const String& title, const String& value,
                             uint32_t titleColor, uint32_t valueColor,
                             uint32_t durationMs,
                             AbortFn abortFn);

  uint32_t arcadeBorderColor(const TetrisGame& g, uint8_t x, uint8_t y, uint32_t nowMs) const;
  uint32_t solidLevelBorderColor(const TetrisGame& g, uint8_t x, uint8_t y, uint32_t nowMs) const;
  void loadDisplaySettings();
  void saveDisplaySettings() const;

private:
  // Brightness / fade state
  uint8_t brightnessApplied_ = 0;
  uint8_t brightnessTarget_ = 0;
  uint8_t fadeFrom_ = 0;
  uint8_t fadeTo_ = 0;
  uint32_t fadeStartMs_ = 0;
  uint32_t fadeDurationMs_ = 0;
  bool fadeActive_ = false;
  bool paused_ = false;
  uint8_t userBrightness_ = (uint8_t)BRIGHTNESS;
  uint8_t rotationQuarterTurns_ = 0; // 0=0, 1=90 CW, 2=180, 3=270 CW

  Adafruit_NeoPixel strip_;
};
