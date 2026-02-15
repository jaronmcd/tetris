#pragma once
#include <stdint.h>
#include <Bluepad32.h>
#include "actions.h"

class InputManager {
public:
  void begin();
  void update();                 // call every loop
  Actions readActions();         // merged Serial + Gamepad

private:
  Actions readSerial();
  Actions readGamepad();

  struct RepeatKey {
    bool lastHeld = false;
    uint32_t nextMs = 0;
  };
  bool fireWithRepeat(bool held, RepeatKey& rk, uint32_t now, uint32_t firstDelayMs, uint32_t repeatMs);

private:
  static void onConnected(GamepadPtr g);
  static void onDisconnected(GamepadPtr g);

  static GamepadPtr gp_;
  bool initialized_ = false;
  RepeatKey repL_, repR_;
  bool lastA_ = false, lastB_ = false, lastStart_ = false;
};
