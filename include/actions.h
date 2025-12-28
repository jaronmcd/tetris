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
};
