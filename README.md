# ESP32 NeoPixel Tetris (16×16) 

A tiny, self-contained **Tetris** for an **ESP32‑S3** driving a **16×16 NeoPixel/WS2812 matrix**.

- **10×16** playfield centered on a 16×16 matrix
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

## One-click web flashing (GitHub Pages)

This repo includes a GitHub Actions workflow that builds the ESP32 firmware and publishes a simple "Flash this" webpage (using ESP Web Tools / Web Serial).

### Enable it in your GitHub repo

1. **Repo Settings → Pages → Build and deployment → Source: "GitHub Actions"**
2. Push to `main` (or run the workflow manually under the Actions tab).
3. Your flasher will be available at:

   `https://<YOUR_GITHUB_USERNAME>.github.io/<YOUR_REPO>/`

### Flash a board

1. Connect the ESP32-S3 over USB (use a data-capable cable).
2. Open the link above in **Chrome** or **Edge**.
3. Click **Install** and select the serial device when prompted.

### Local (no GitHub)

After you build with PlatformIO, you can generate the same web flasher site locally:

```bash
pio run -e esp32-s3-4mb
python scripts/ci/build_webflash_site.py --env esp32-s3-4mb --out site
python -m http.server --directory site 8000
```

Then browse to `http://localhost:8000`.

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
- **0 / 1 / 2 / 3** → set AI speed profile (slow → turbo)

The firmware prints a reminder of these keys on boot over Serial.

---

## Configuration

All the project’s main tuning knobs live in:

- `include/config.h`

Useful settings:

- `LED_PIN` – data pin for the matrix (default: **GPIO 1**)
- `MATRIX_W`, `MATRIX_H` – matrix dimensions (default: **16×16**)
- `BRIGHTNESS` – global brightness (default: **14**)
- `SERPENTINE`, `MATRIX_BOTTOM_UP` – adjust if your matrix is wired/oriented differently
- `BOARD_OFFSET_X`, `BOARD_OFFSET_Y` – where the 10×16 Tetris board sits inside the matrix
- `RESET_SCORES_ON_BOOT` – wipe saved score/level on boot
- `AI_SAVES_HIGH_SCORE` – allow demo AI to write new records

High scores are stored using ESP32 **Preferences** under namespace `tetris` with keys `hs` and `hl`.

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
