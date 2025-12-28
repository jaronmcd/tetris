#include <Arduino.h>
#include "tetris.h"
#include "display_matrix.h"
#include "input.h"
#include "actions.h"

static TetrisGame game;
static MatrixDisplay display;
static InputManager input;

static uint32_t lastFrameMs = 0;

void setup() {
  Serial.begin(115200);
  delay(200);

  display.begin();
  display.bootFlash();

  input.begin();
  game.begin();

  Serial.println("\nTetris ready.");
  Serial.println("Xbox: D-pad move, A=rotate, B=drop, Y=restart.");
  Serial.println("Serial: a/d/w/s/space, r=restart");
}

void loop() {
  input.update();

  uint32_t now = millis();
  Actions a = input.readActions();

  auto tr = game.tick(now, a);
  if (tr.levelUp) {
    display.levelUpFlash();
    Serial.printf("Level %u\n", game.level());
  }

  // Render ~30 FPS
  if ((now - lastFrameMs) >= 33) {
    lastFrameMs = now;
    display.render(game, now);
  }
}
