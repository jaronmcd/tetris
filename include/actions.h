#pragma once

#include <stdint.h>

struct Actions {
  bool left = false;
  bool right = false;
  bool rotate = false;
  bool down = false;   // held for soft drop
  bool drop = false;   // hard drop
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

};
