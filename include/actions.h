#pragma once

#include <stdint.h>

struct Actions {
  bool left = false;
  bool right = false;
  bool rotate = false;
  bool down = false;   // held for soft drop
  bool drop = false;   // hard drop
  bool togglePause = false;
  bool pauseResetHeld = false;  // held while paused to trigger reset-to-intro
  bool restart = false;

  // Debug / commands (not treated as player input)
  // -1 = no command, 0..3 = set AI speed profile
  int8_t aiProfileSet = -1;

  // Debug: force trigger line-clear FX for testing (serial-only)
  // 0 = none, 1..4 = clear that many lines
  uint8_t testClearLines = 0;

  // Debug: bump level up/down for testing palettes & transitions (serial-only)
  // 0 = none, negative = down, positive = up
  int8_t testLevelDelta = 0;

  // Debug: toggle "high score border" mode for testing (serial-only)
  bool toggleHighScoreBorders = false;

  // Debug: preview high-score / end-of-run screens (serial-only)
  // 0 = none
  // 1 = boot stats (MAX level)
  // 2 = game over (current + max, using live values)
  // 3 = game over (forced non-record, always shows both screens)
  // 4 = game over (forced tie/record style)
  // 5 = new MAX level celebration
  // 6 = MAX chase progress preview (cycles attempts fill)
  uint8_t previewScreen = 0;

};
