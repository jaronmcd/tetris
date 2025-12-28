#pragma once

struct Actions {
  bool left = false;
  bool right = false;
  bool rotate = false;
  bool down = false;   // held for soft drop
  bool drop = false;   // hard drop
  bool restart = false;
};
