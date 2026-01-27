#include "input.h"
#include <Arduino.h>
#include "config.h"

GamepadPtr InputManager::gp_ = nullptr;

void InputManager::onConnected(GamepadPtr g) {
  gp_ = g;
  Serial.println("Gamepad connected!");
}

void InputManager::onDisconnected(GamepadPtr) {
  gp_ = nullptr;
  Serial.println("Gamepad disconnected!");
}

void InputManager::begin() {
  BP32.setup(&InputManager::onConnected, &InputManager::onDisconnected);
  BP32.enableNewBluetoothConnections(true);

  // Uncomment once if you want to force re-pairing during testing:
  // BP32.forgetBluetoothKeys();
}

void InputManager::update() {
  BP32.update();
}

bool InputManager::fireWithRepeat(bool held, RepeatKey& rk, uint32_t now,
                                  uint32_t firstDelayMs, uint32_t repeatMs) {
  if (!held) {
    rk.lastHeld = false;
    rk.nextMs = 0;
    return false;
  }
  if (!rk.lastHeld) {
    rk.lastHeld = true;
    rk.nextMs = now + firstDelayMs;
    return true; // immediate on press
  }
  if (now >= rk.nextMs) {
    rk.nextMs = now + repeatMs;
    return true;
  }
  return false;
}

Actions InputManager::readSerial() {
  Actions a;
  while (Serial.available() > 0) {
    char ch = (char)Serial.read();
    if (ch == 'a' || ch == 'A') a.left = true;
    else if (ch == 'd' || ch == 'D') a.right = true;
    else if (ch == 'w' || ch == 'W') a.rotate = true;
    else if (ch == 's' || ch == 'S') a.down = true;
    else if (ch == ' ') a.drop = true;
    else if (ch == 'r' || ch == 'R') a.restart = true;
    else if (ch >= '0' && ch <= '3') a.aiProfileSet = (int8_t)(ch - '0');
    else if (ch == 'z' || ch == 'Z') a.testClearLines = 1;
    else if (ch == 'x' || ch == 'X') a.testClearLines = 2;
    else if (ch == 'c' || ch == 'C') a.testClearLines = 3;
    else if (ch == 'v' || ch == 'V') a.testClearLines = 4;

    // Debug: level stepping (serial-only)
    //   ]  = level +1
    //   [  = level -1
    //   }  = level +10
    //   {  = level -10
    else if (ch == ']') a.testLevelDelta += 1;
    else if (ch == '[') a.testLevelDelta -= 1;
    else if (ch == '}') a.testLevelDelta += 10;
    else if (ch == '{') a.testLevelDelta -= 10;
    else if (ch == 'h' || ch == 'H') a.toggleHighScoreBorders = true;

    // Debug: preview high-score screens (serial-only)
    //   b = boot stats (MAX level)
    //   o = game over (live values)
    //   p = game over (forced non-record)
    //   t = game over (forced tie/record style)
    //   m = new MAX level celebration
    //   g = MAX chase progress preview (cycles fill/colors)
    else if (ch == 'b' || ch == 'B') a.previewScreen = 1;
    else if (ch == 'o' || ch == 'O') a.previewScreen = 2;
    else if (ch == 'p' || ch == 'P') a.previewScreen = 3;
    else if (ch == 't' || ch == 'T') a.previewScreen = 4;
    else if (ch == 'm' || ch == 'M') a.previewScreen = 5;
    else if (ch == 'g' || ch == 'G') a.previewScreen = 6;
  }
  return a;
}

Actions InputManager::readGamepad() {
  Actions a;
  if (!gp_ || !gp_->isConnected()) return a;

  uint8_t d = gp_->dpad();
  bool leftHeld  = d & DPAD_LEFT;
  bool rightHeld = d & DPAD_RIGHT;
  bool downHeld  = d & DPAD_DOWN;

  uint32_t now = millis();

  // Classic-ish repeat (DAS)
  if (fireWithRepeat(leftHeld,  repL_, now, 170, 90)) a.left = true;
  if (fireWithRepeat(rightHeld, repR_, now, 170, 90)) a.right = true;

  a.down = downHeld;

  bool A = gp_->a();
  bool B = gp_->b();
  bool Y = gp_->y();

  if (A && !lastA_) a.rotate = true;
  if (B && !lastB_) a.drop = true;
  if (Y && !lastY_) a.restart = true;

  lastA_ = A;
  lastB_ = B;
  lastY_ = Y;

  return a;
}

Actions InputManager::readActions() {
  Actions s = readSerial();
  Actions g = readGamepad();

  Actions out;
  out.left = s.left || g.left;
  out.right = s.right || g.right;
  out.rotate = s.rotate || g.rotate;
  out.down = s.down || g.down;
  out.drop = s.drop || g.drop;
  out.restart = s.restart || g.restart;

  // Commands (serial-only)
  out.aiProfileSet = s.aiProfileSet;
  out.testClearLines = s.testClearLines;
  out.testLevelDelta = s.testLevelDelta;
  out.toggleHighScoreBorders = s.toggleHighScoreBorders;
  out.previewScreen = s.previewScreen;
  return out;
}
