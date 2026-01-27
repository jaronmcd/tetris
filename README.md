# ESP32 NeoPixel Tetris (16×16) 

A tiny, self-contained **Tetris** for an **ESP32‑S3** driving a **16×16 NeoPixel/WS2812 matrix**.

- **10×15** playfield centered on a 16×16 matrix (bottom row reserved as border)
- **Bluetooth gamepad support** via **Bluepad32** (D‑pad + buttons)
- **Idle → AI demo mode** (kicks in after a few seconds with no input)
- Persistent **High Score** + **High Level** stored in ESP32 **NVS/Preferences**
- Animated, level-based **border themes** that **react to the falling piece** (subtle “sphere of light”)
- Boot splash + skippable boot stats screen

---

## Hardware

- ESP32‑S3 DevKit (project defaults to `esp32-s3-devkitc-1`)
- 16×16 NeoPixel / WS2812(B) matrix (256 LEDs)
- 5V power supply sized for your matrix
  - Even with the default low brightness, *don’t* power the matrix from the dev board’s USB.
- Common ground between ESP32 and LED matrix

### Wiring (default)

| Signal | ESP32-S3 | Matrix |
|---|---:|---|
| Data | **GPIO 1** (`LED_PIN`) | DIN |
| Power | 5V supply | +5V |
| Ground | GND | GND |

If your board/matrix uses a different data pin, change it in `include/config.h`.

---

## Software Setup

This is a **PlatformIO** project.

### Requirements

- VS Code + PlatformIO extension (recommended)
- Or PlatformIO CLI

The build uses a Bluepad32-enabled Arduino core (see `platformio.ini`).

---

## Build, Upload, Monitor

From the project root:

```bash
pio run -e esp32-s3-4mb
pio run -e esp32-s3-4mb -t upload
pio device monitor -b 115200
```

Or in one shot:

```bash
pio run -e esp32-s3-4mb -t upload -t monitor
```

---

## Controls

### Bluetooth gamepad (Bluepad32)

- **D‑pad Left/Right**: move
- **D‑pad Down (hold)**: soft drop
- **A**: rotate
- **B**: hard drop
- **Y**: restart

### Serial keyboard (via monitor)

- **A / D**: move left/right
- **W**: rotate
- **S (hold)**: soft drop
- **Space**: hard drop
- **R**: restart

---

## Debug / Test Keys (Serial)

These are handy for quickly testing animations without playing a full game:

- **z / x / c / v** → force **1 / 2 / 3 / 4** line‑clear FX
- **[ / ]** → level **-1 / +1** (animates transition)
- **{ / }** → level **-10 / +10**
- **h** → toggle **“high score” border style** (forces the rainbow arcade border)
- **b** → preview **boot stats** (MAX level screen)
- **o** → preview **game over** (live: current level + saved MAX)
- **p** → preview **game over** (forced non-record: always shows both screens)
- **t** → preview **game over** (forced tie/record style)
- **m** → preview **new MAX level celebration**
- **g** → preview **MAX chase progress** (cycles fill + colors)
- **0 / 1 / 2 / 3** → set AI speed profile (slow → turbo)

The firmware prints a reminder of these keys on boot over Serial.

---

## Configuration

All the project’s main tuning knobs live in:

- `include/config.h`

Useful settings:

- `LED_PIN` – data pin for the matrix (default: **GPIO 1**)
- `MATRIX_W`, `MATRIX_H` – matrix dimensions (default: **16×16**)
- `BRIGHTNESS` – global brightness (default: **95**)
- `SERPENTINE`, `MATRIX_BOTTOM_UP` – adjust if your matrix is wired/oriented differently
- `BOARD_OFFSET_X`, `BOARD_OFFSET_Y` – where the 10×16 Tetris board sits inside the matrix
- `LEVEL_NUMBER_DROPDOWN_ENABLED` – show/hide the in-game level number “drop-down” overlay during level transitions
- `MILESTONE_BORDER_REVEAL_ENABLED` – border-only celebration when entering a new 10-level border style (levels 11/21/31/...)
- `MILESTONE_BORDER_REVEAL_MS` – duration (ms) of the milestone border reveal animation
- `MAX_LEVEL_CHASE_PROGRESS_ENABLED` – enable the MAX-level "attempts" progress background (boot + game over)
- `MAX_LEVEL_CHASE_PROGRESS_STEPS` – steps to fully fill the progress background (16 = 1 row per attempt on a 16x16)
  - Color cycles: every time the meter fully fills, the MAX background advances to a new hue and keeps going.
- `RESET_SCORES_ON_BOOT` – wipe saved score/level on boot
- `AI_SAVES_HIGH_SCORE` – allow demo AI to write new records

AI smartness ladder (tied to MAX chase progress):

- `AI_SMARTNESS_FROM_MAX_CHASE_ENABLED` – if true, every time the MAX chase progress hits a *full* background color, the AI increases its decision quality
- `AI_SMARTNESS_BASE` – starting skill level (1 = current baseline; 0 is intentionally a little sloppy for testing)
- `AI_SMARTNESS_MAX` – max skill level (keeps MCU CPU usage predictable)

Records are stored using ESP32 **Preferences** under namespace `tetris`:

- `ml` = MAX level ever reached (primary record for the tiny LED UI)
- `hs` = classic score (points)
- `hl` = level associated with `hs`
- `pg` = has-played flag (used to gate "chasing the record" FX)
- `ma` = MAX-level chase attempts counter (completed record-eligible runs since `ml` was last updated)

---

## How It Works (Project Layout)

- `src/main.cpp` – boot flow, demo AI mode, main loop
- `src/tetris.cpp` – Tetris rules, scoring, line clear timing, high score storage
- `src/display_matrix_*.cpp` – NeoPixel rendering, borders, text, animations
- `src/input.cpp` – Bluepad32 + Serial input merged into a single `Actions` struct
- `src/ai.cpp` – simple AI for demo/attract mode

---

## Troubleshooting

- **Nothing lights up**
  - Confirm the matrix has **5V power** and **shared ground** with the ESP32.
  - Verify `LED_PIN` in `include/config.h` matches your wiring.

- **Matrix looks mirrored / scrambled**
  - Toggle `SERPENTINE` or `MATRIX_BOTTOM_UP` in `include/config.h`.
  - If your matrix is physically rotated, you may need to adjust offsets or mapping.

- **Bluetooth gamepad won’t connect**
  - Bluepad32 pairing is handled by the firmware; try power-cycling.
  - During development you can optionally clear pairings in `src/input.cpp` by calling `BP32.forgetBluetoothKeys()` (commented).

---

## Roadmap Ideas

- Add sound (buzzer) or haptics
- Add a “hold piece” mechanic
- Per‑level soundtrack / more border styles
- Save additional stats (lines, games played, etc.)

---

## License

No license file is included yet. If you plan to open-source this, add a `LICENSE` file (MIT/Apache-2.0/GPL/etc.) and update this section.
