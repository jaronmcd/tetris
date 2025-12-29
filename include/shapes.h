#pragma once
#include <stdint.h>

// Shared 4x4 bitmasks for the 7 classic Tetris pieces (I,O,T,S,Z,J,L).
//
// Centralizing this table avoids accidental mismatches between the game logic
// and the AI evaluator.

extern const uint16_t TETRIS_SHAPES[7][4];
