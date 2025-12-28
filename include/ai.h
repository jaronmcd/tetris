#pragma once
#include <stdint.h>
#include "actions.h"
#include "tetris.h"

class TetrisAI {
public:
  void reset();
  Actions think(const TetrisGame& g, uint32_t nowMs);

private:
  struct Plan {
    bool valid = false;
    uint8_t rot = 0;   // 0..3
    int8_t x = 3;      // target cur.x
  };

  enum Phase : uint8_t {
    WAIT_THINK = 0,
    ROTATE_TO_TARGET,
    MOVE_TO_TARGET,
    SOFT_DROP
  };

  Plan computePlan(const TetrisGame& g) const;

  uint32_t jitterMs(uint32_t base, uint32_t spread) const;

private:
  uint32_t lastPieceSeq_ = 0;

  Phase phase_ = WAIT_THINK;
  uint32_t phaseUntilMs_ = 0;     // used for "thinking" delay
  uint32_t nextStepMs_ = 0;       // rate-limit button presses

  // soft-drop pulsing to look human
  bool downPulseOn_ = true;
  uint32_t nextDownToggleMs_ = 0;

  Plan plan_;
};
