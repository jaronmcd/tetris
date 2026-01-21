#pragma once
#include <Arduino.h>
#include <stdint.h>
#include <Adafruit_NeoPixel.h>

#include "config.h"
#include "tetris.h"

class MatrixDisplay {
public:
  using AbortFn = bool (*)();

  MatrixDisplay();

  void begin();
  void bootFlash();

  // Call frequently (e.g., once per loop iteration). When enabled in config,
  // this dynamically selects between "USB host" brightness (PC/hub) and
  // "USB charger" brightness (no enumeration).
  void tickUsbPowerBrightness(uint32_t nowMs);

  // Debug: force the "high score" border style regardless of score.
  void setDebugForceHighScoreBorders(bool enable);
  void levelUpFlash(uint8_t nextLevel);

  // Debug / test helper: animate the palette/border transition between ANY two levels.
  // (Used by serial level-stepping keys.)
  void levelTransition(uint8_t fromLevel, uint8_t toLevel);

  // Optional: Boot splash/logo screen (skippable)
  void showBootLogo(uint32_t durationMs, AbortFn abortFn = nullptr);

  // Normal Game Over (Score + Level)
  void showGameOver(uint8_t level, uint8_t maxLevel);

  // Celebration for beating the max-level record
  void showNewMaxLevel(uint8_t maxLevel);

  // Boot: shows MAX level (skippable)
  void showBootStats(uint8_t maxLevel, AbortFn abortFn = nullptr);

  void render(const TetrisGame& g, uint32_t nowMs);

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
  void drawNumberCentered(uint8_t value, uint8_t scale, uint32_t color);
  bool showLevelNumberScreen(uint8_t value, uint32_t bg, uint32_t fg, uint32_t durationMs, AbortFn abortFn);

  // Two-line screen: title on top, value on bottom (scrolls if needed). Returns true if aborted.
  bool showTwoLineTitleValue(const String& title, const String& value,
                             uint32_t titleColor, uint32_t valueColor,
                             uint32_t durationMs,
                             AbortFn abortFn);

  uint32_t arcadeBorderColor(const TetrisGame& g, uint8_t x, uint8_t y, uint32_t nowMs) const;
  uint32_t solidLevelBorderColor(const TetrisGame& g, uint8_t x, uint8_t y, uint32_t nowMs) const;

  void applyBrightness(uint8_t b);

private:
  Adafruit_NeoPixel strip_;

  // USB power-aware brightness state
  enum class UsbPowerMode : uint8_t {
    Unknown = 0,
    Host = 1,
    Charger = 2,
  };

  uint32_t usbBrightnessStartMs_ = 0;
  uint8_t brightnessApplied_ = 0;
  UsbPowerMode usbMode_ = UsbPowerMode::Unknown;
};
