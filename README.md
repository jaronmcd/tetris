# ESP32 NeoPixel Tetris (16×16) 

A tiny, self-contained **arcade bundle** for an **ESP32** driving a **16×16 NeoPixel/WS2812 matrix**.

- Game menu with **2 games**: **Tetris** and **Breakout**
- Persistent boot mode: boots directly into the last selected game (**default: Tetris**)
- **Tetris** uses a **10×15** playfield centered on a 16×16 matrix (bottom row reserved as border)
- **Bluetooth gamepad support** via **Bluepad32** (D‑pad + buttons)
- **Idle → AI demo mode** (kicks in after a few seconds with no input)
- Display settings screen: adjust brightness + screen rotation
- Independent persistent highscores for **Tetris** and **Breakout** (ESP32 **NVS/Preferences**)
- Animated, level-based **border themes** that **react to the falling piece** (subtle “sphere of light”)
- Boot splash + skippable boot stats screen

---

## Web Flash (GitHub Pages)

You can flash firmware directly from your browser at:

- <https://jaronmcd.github.io/tetris/>

Notes:

- Current web installer target is **ESP32 Dev Module** (`esp32dev-4mb`) only.
- Browser support: Chrome/Edge with Web Serial.
- If you fork this repo, your Pages URL will be:
  - `https://<your-github-username>.github.io/tetris/`

---

## Hardware

- ESP32 DevKit V1 / ESP-WROOM-32 board (project default env: `esp32dev-4mb`)
- ESP32-S3 DevKitC-1 (optional env: `esp32-s3-4mb`)
- 16×16 NeoPixel / WS2812(B) matrix (256 LEDs)
- 5V power supply sized for your matrix
  - Even with the default low brightness, *don’t* power the matrix from the dev board’s USB.
- Common ground between ESP32 and LED matrix

### Wiring

Default environment: `esp32dev-4mb`

| Signal | ESP32 DevKit (ESP-WROOM-32) | Matrix |
|---|---:|---|
| Data | **GPIO 23** (`LED_PIN` in `esp32dev-4mb`) | DIN |
| Power | 5V supply | +5V |
| Ground | GND | GND |

Optional environment: `esp32-s3-4mb`

| Signal | ESP32-S3 DevKitC-1 | Matrix |
|---|---:|---|
| Data | **GPIO 1** (`LED_PIN` in `esp32-s3-4mb`) | DIN |
| Power | 5V supply | +5V |
| Ground | GND | GND |

If your board/matrix uses a different data pin, update `LED_PIN` in `platformio.ini` (`build_flags`) or `include/config.h`.

### Microcontroller Environments (`platformio.ini`)

| Environment | Board | Default LED data pin | USB/Serial behavior |
|---|---|---:|---|
| `esp32dev-4mb` | `esp32dev` (ESP-WROOM-32 DevKit) | GPIO 23 | External USB-UART bridge (CP2102/CH340). Repo currently pins `upload_port` + `monitor_port` to `/dev/ttyUSB0`. |
| `esp32-s3-4mb` | `esp32-s3-devkitc-1` | GPIO 1 | Native USB CDC (`ARDUINO_USB_MODE=1`, `ARDUINO_USB_CDC_ON_BOOT=1`). Usually appears as `/dev/ttyACM*` on Linux. |

If you have multiple USB-UART adapters, prefer a stable path such as `/dev/serial/by-id/...` for `upload_port` and `monitor_port` in your selected environment.

---

## Build One (End-to-End)

If you want to make one from scratch, follow this path:

1. Gather parts
   - ESP32 dev board (project default env: `esp32dev-4mb`)
   - 16x16 WS2812(B)/NeoPixel **5V** matrix panel (256 addressable RGB LEDs)
   - For compatible alternatives, match these display specs:
     - 16x16 layout (256 pixels total)
     - WS2812(B)/NeoPixel protocol over a single data line (`DIN`)
     - 3-wire power/data interface (`5V`, `GND`, `DIN`) with shared ground
     - Marked `DIN` / `DOUT` pads (or connector) for orientation/chaining
   - Pixel pitch and panel size can vary; firmware only requires a logical 16x16 matrix
   - Example listing used in this build (may change): <https://a.co/d/01Z0GEtj>
   - 5V power supply (3A+ recommended)
   - Data-capable USB cable
   - Jumper wires
   - Optional but recommended: 330-470 ohm resistor on `DIN`, 1000uF capacitor across matrix `+5V/GND`
2. Wire it
   - Matrix `DIN` -> ESP32 `GPIO 23` (`esp32dev-4mb`) or `GPIO 1` (`esp32-s3-4mb`)
   - Matrix `+5V` -> 5V supply
   - Matrix `GND` -> 5V supply ground
   - ESP32 `GND` -> same ground as the matrix/power supply
3. Flash firmware
   - From this repo root:
     ```bash
     pio run -e esp32dev-4mb
     pio run -e esp32dev-4mb -t upload
     pio device monitor -b 115200
     ```
4. Pair a controller and play
   - Connect a Bluetooth gamepad (or use serial keyboard controls listed below).

Tip: If nothing lights up, check power and shared ground first, then verify the active environment’s `LED_PIN` in `platformio.ini`.

---

## Software Setup

This is a **PlatformIO** project.

### Requirements

- VS Code + PlatformIO extension (recommended)
- Or PlatformIO CLI

The build uses a Bluepad32-enabled Arduino core (see `platformio.ini`).

---

## Build, Upload, Monitor

From the project root.

Default board (`esp32dev-4mb`):

```bash
pio run -e esp32dev-4mb
pio run -e esp32dev-4mb -t upload
pio device monitor -b 115200
```

ESP32-S3 board (`esp32-s3-4mb`):

```bash
pio run -e esp32-s3-4mb
pio run -e esp32-s3-4mb -t upload
pio device monitor -b 115200
```

Or in one shot for either environment:

```bash
pio run -e esp32dev-4mb -t upload -t monitor
```

If serial-port autodetection picks the wrong adapter, set `upload_port`/`monitor_port` in `platformio.ini` for that environment.

---

## Controls

### Game menu switch

- While **paused**, hold **SELECT/BACK/SHARE** to run a **3, 2, 1** countdown to the game menu (release to cancel)
- **Left / Right**: choose game
- **A / B / START**: launch selected game
- Selected game becomes the new persistent boot mode
- In game menu: hold **SELECT/BACK/SHARE** for ~3 seconds to open **Display Settings**
- **Serial:** `W` / `Space` / `R` also launch from the menu

### Display settings (from game menu)

- **Up / Down**: brightness up/down
- **Left / Right**: rotate screen (0 / 90 / 180 / 270)
- Hold **SELECT/BACK/SHARE** for ~2.5 seconds to restart to intro (TV-style power-off animation, no MCU reboot)
- **A / B / START**: exit settings and return to game menu

### Bluetooth gamepad (Bluepad32)

#### Tetris

- **D‑pad Left/Right**: move
- **D‑pad Down (hold)**: soft drop
- **A**: rotate
- **B**: hard drop
- **START**: pause/resume (screen dims while paused)
- **SELECT/BACK/SHARE** (while paused): run 3-second menu countdown; releasing cancels

#### Breakout

- **D‑pad Left/Right**: move paddle
- **A / B / D‑pad Down**: launch ball (and restart after game over)
- **START**: pause/resume
- **SELECT/BACK/SHARE** (while paused): run 3-second menu countdown; releasing cancels
- Idle for a few seconds to enable **Breakout AI demo mode** (any input returns to human control)

### Serial keyboard (via monitor)

#### Tetris

- **A / D**: move left/right
- **W**: rotate
- **S (hold)**: soft drop
- **Space**: hard drop
- **R**: restart

#### Breakout

- **A / D**: move paddle
- **W / Space / S**: launch ball (and restart after game over)
- **R**: return to game menu

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
- **0 / 1 / 2 / 3** → set AI speed profile (slow → turbo) for both Tetris and Breakout demos

The firmware prints a reminder of these keys on boot over Serial.

---

## Configuration

All the project’s main tuning knobs live in:

- `include/config.h`

Useful settings:

- `LED_PIN` – data pin for the matrix (default: **GPIO 23** in `esp32dev-4mb`; **GPIO 1** in `esp32-s3-4mb`)
- `MATRIX_W`, `MATRIX_H` – matrix dimensions (default: **16×16**)
- `BRIGHTNESS` – global brightness (default: **95**)
- `PAUSE_DIM_ENABLED` – dim matrix while paused (default: **true**)
- `PAUSE_BRIGHTNESS_WHEN_PAUSED` – brightness while paused (default: **22**)
- `SERPENTINE`, `MATRIX_BOTTOM_UP` – adjust if your matrix is wired/oriented differently
- `BOARD_OFFSET_X`, `BOARD_OFFSET_Y` – where the 10×16 Tetris board sits inside the matrix
- `BOOT_STATS_ENABLED` – show the MAX-level "high screen" status during power-up (skippable)
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
- `AI_ADAPTIVE_EVOLUTION_ENABLED` – adds a board-stress adaptive layer on top of the MAX-chase ladder
- `AI_ADAPTIVE_PRESSURE_START_ROW` – top-stack row where adaptive pressure begins
- `AI_ADAPTIVE_PRESSURE_FULL_ROW` – top-stack row where adaptive pressure is treated as maxed
- `AI_ADAPTIVE_RAMP_BONUS_MAX_PCT` – max extra ramp added toward the next AI skill tier
- `AI_ADAPTIVE_SKILL_BOOST_ON_PCT` – adaptive signal threshold to temporarily add `+1` skill
- `AI_ADAPTIVE_SKILL_BOOST_OFF_PCT` – lower threshold to drop that temporary boost (hysteresis)

Records are stored using ESP32 **Preferences** under namespace `tetris`:

- `ml` = MAX level ever reached (primary record for the tiny LED UI)
- `hs` = classic score (points)
- `hl` = level associated with `hs`
- `pg` = has-played flag (used to gate "chasing the record" FX)
- `ma` = MAX-level chase attempts counter (completed record-eligible runs since `ml` was last updated)

Breakout records are stored independently in namespace `breakout`:

- `hs` = Breakout high score

Boot mode preference is stored in namespace `arcade`:

- `bootm` = selected boot mode (`0`=Tetris, `1`=Breakout)

---

## How It Works (Project Layout)

- `src/main.cpp` – boot flow, game-select menu, mode switching, main loop
- `src/breakout.cpp` – Breakout rules, paddle/ball physics, brick field, lives
- `src/tetris.cpp` – Tetris rules, scoring, line clear timing, high score storage
- `src/display_matrix_*.cpp` – NeoPixel rendering, borders, text, animations
- `src/input.cpp` – Bluepad32 + Serial input merged into a single `Actions` struct
- `src/ai.cpp` – simple AI for demo/attract mode

---

## Troubleshooting

- **Nothing lights up**
  - Confirm the matrix has **5V power** and **shared ground** with the ESP32.
  - Verify the active environment’s `LED_PIN` in `platformio.ini` matches your wiring.

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

## Contributing & Security

- See `CONTRIBUTING.md` for development and PR guidelines.
- Report security issues privately as described in `SECURITY.md`.

---

## License

This project is licensed under the MIT License. See `LICENSE`.
