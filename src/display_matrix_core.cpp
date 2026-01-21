#include "display_matrix.h"

#include <Arduino.h>

#if USB_BRIGHTNESS_AUTO_ENABLED
  #if __has_include("tusb.h")
    #include "tusb.h"
    #define TETRIS_HAS_TINYUSB_MOUNTED 1
  #endif
#endif

static inline bool usbHostEnumerated() {
#ifdef TETRIS_HAS_TINYUSB_MOUNTED
  return tud_mounted();
#else
  return false;
#endif
}


static inline bool usbCdcPortOpen() {
#if USB_BRIGHTNESS_USE_SERIAL_OPEN_FALLBACK
  // On native USB-CDC builds, bool(Serial) becomes true when the host opens
  // the serial port (e.g., PlatformIO monitor / a terminal). This is a useful
  // fallback if tud_mounted() is unavailable or unreliable on a given stack.
  #if defined(ARDUINO_USB_CDC_ON_BOOT) && (ARDUINO_USB_CDC_ON_BOOT)
    return (bool)Serial;
  #else
    return false;
  #endif
#else
  return false;
#endif
}

MatrixDisplay::MatrixDisplay()
  : strip_(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800) {}

void MatrixDisplay::applyBrightness(uint8_t b) {
  if (brightnessApplied_ == b) return;
  brightnessApplied_ = b;
  strip_.setBrightness(b);
}

void MatrixDisplay::begin() {
#if USB_BRIGHTNESS_AUTO_ENABLED
  usbBrightnessStartMs_ = millis();
  usbMode_ = UsbPowerMode::Unknown;
  applyBrightness((uint8_t)USB_BRIGHTNESS_WHEN_HOST);
#else
  applyBrightness((uint8_t)BRIGHTNESS);
#endif

  strip_.begin();
  strip_.clear();
  strip_.show();
}

void MatrixDisplay::tickUsbPowerBrightness(uint32_t nowMs) {
#if !USB_BRIGHTNESS_AUTO_ENABLED
  (void)nowMs;
  return;
#else
  const bool isHostSignal = usbHostEnumerated() || usbCdcPortOpen();

  // Once we have evidence of a USB host, latch "Host" behavior for the rest
  // of the session (prevents bright flashes during dev resets/enumeration).
  // If we've already latched Charger and host-override is disabled, ignore any
  // late host signals.
  const bool acceptHost = isHostSignal && (usbMode_ != UsbPowerMode::Charger || USB_BRIGHTNESS_HOST_OVERRIDE);
  if (acceptHost) {
    usbMode_ = UsbPowerMode::Host;
    applyBrightness((uint8_t)USB_BRIGHTNESS_WHEN_HOST);
    return;
  }

  // If we already latched "Host", stay there.
  if (usbMode_ == UsbPowerMode::Host) {
    applyBrightness((uint8_t)USB_BRIGHTNESS_WHEN_HOST);
    return;
  }

  // Charger mode (latched). Optionally allow a late host detection to pull
  // us back to dim, but only if enabled.
  if (usbMode_ == UsbPowerMode::Charger) {
    applyBrightness((uint8_t)BRIGHTNESS);
    return;
  }

  // Unknown: start dim, then after a delay assume charger and go bright.
  applyBrightness((uint8_t)USB_BRIGHTNESS_WHEN_HOST);

  const uint32_t elapsed = (uint32_t)(nowMs - usbBrightnessStartMs_);
  if (elapsed >= (uint32_t)USB_BRIGHTNESS_CHARGER_DELAY_MS) {
    usbMode_ = UsbPowerMode::Charger;
    applyBrightness((uint8_t)BRIGHTNESS);
  }
#endif
}

void MatrixDisplay::bootFlash() {
  strip_.clear();
  strip_.fill(strip_.Color(255, 0, 0));
  strip_.show();
  delay(120);

  strip_.clear();
  strip_.fill(strip_.Color(0, 255, 0));
  strip_.show();
  delay(120);

  strip_.clear();
  strip_.fill(strip_.Color(0, 0, 255));
  strip_.show();
  delay(120);

  strip_.clear();
  strip_.show();
}

uint16_t MatrixDisplay::XY(uint8_t x, uint8_t y) const {
  if (x >= MATRIX_W || y >= MATRIX_H) return 0;
  // Match original physical mapping:
  // - X is mirrored (MATRIX_W-1-x)
  // - Optional Y flip via MATRIX_BOTTOM_UP
  // - Optional serpentine wiring via SERPENTINE
  uint8_t xx = (uint8_t)(MATRIX_W - 1 - x);
  uint8_t yy = MATRIX_BOTTOM_UP ? (uint8_t)(MATRIX_H - 1 - y) : y;
  if (!SERPENTINE) return (uint16_t)yy * MATRIX_W + xx;
  if ((yy & 1) == 0) return (uint16_t)yy * MATRIX_W + xx;
  return (uint16_t)yy * MATRIX_W + (MATRIX_W - 1 - xx);
}

void MatrixDisplay::setPixel(int16_t x, int16_t y, uint32_t c) {
  if (x < 0 || x >= MATRIX_W || y < 0 || y >= MATRIX_H) return;
  strip_.setPixelColor(XY((uint8_t)x, (uint8_t)y), c);
}

uint32_t MatrixDisplay::rgb(uint32_t rrggbb) const {
  uint8_t r = (rrggbb >> 16) & 0xFF;
  uint8_t g = (rrggbb >> 8) & 0xFF;
  uint8_t b = (rrggbb) & 0xFF;
  return strip_.Color(r, g, b);
}
