#include "leds.h"
#include <Adafruit_NeoPixel.h>
#include "hid.h"
#include "pairing.h"
#include "bootguard.h"

static Adafruit_NeoPixel strip(8, PIN_LED, NEO_GRB + NEO_KHZ800);
static uint32_t ringUntil = 0, ringColor = 0;
static uint32_t ovUntil[8], ovColor[8];
static bool lastPairing = false;
static int lastKnown = -1;
static uint32_t pairStartedAt = 0;

// Brightness is low on purpose: 8 LEDs at full tilt plus Wi-Fi TX can brown out a USB-powered board.
void leds_begin() { strip.begin(); strip.setBrightness(32); strip.clear(); strip.show(); }

void leds_flash(uint8_t r, uint8_t g, uint8_t b, uint16_t ms) { ringColor = strip.Color(r, g, b); ringUntil = millis() + ms; }

void leds_pad_event(int slot, PadEvent ev) {
  int led = slot < 7 ? slot : 6;
  uint32_t t = millis();
  if (ev == PADEV_CONNECTED || ev == PADEV_READY) { ovColor[led] = strip.Color(255, 255, 255); ovUntil[led] = t + 500; }
  else if (ev == PADEV_DISCONNECTED) { ovColor[led] = strip.Color(255, 0, 0); ovUntil[led] = t + 900; }
}

static uint32_t col(uint8_t r, uint8_t g, uint8_t b) { return strip.Color(r, g, b); }

void leds_tick() {
  static uint32_t last = 0;
  uint32_t now = millis();
  if (now - last < 40) return;
  last = now;

  bool pairing = pairing_active();
  int known = pairing_count();
  if (pairing && !lastPairing) { leds_flash(140, 140, 140, 350); pairStartedAt = now; }
  if (!pairing && lastPairing) {
    bool paired = lastKnown >= 0 && known > lastKnown;
    if (!paired) { /* timed out or cancelled: amber double flash */ ringColor = col(255, 140, 0); ringUntil = now + 700; }
  }
  if (lastKnown >= 0 && known > lastKnown) leds_flash(0, 255, 0, 1800);
  lastPairing = pairing;
  lastKnown = known;

  strip.clear();
  float hold = boot_hold_progress();
  bool ringFlash = (int32_t)(ringUntil - now) > 0;
  bool amberGap = ringFlash && ringColor == col(255, 140, 0) && ((now / 140) % 2);

  if (hold > 0.4f) {
    int n = (int)((hold - 0.4f) / 0.6f * 8.0f) + 1;
    for (int i = 0; i < n && i < 8; i++) strip.setPixelColor(i, col(255, 0, 0));
  } else if (ringFlash && !amberGap) {
    for (int i = 0; i < 8; i++) strip.setPixelColor(i, ringColor);
  } else if (pairing) {
    bool urgent = pairing_ms_left() < 10000;
    int head = (now / (urgent ? 60 : 90)) % 8;
    for (int i = 0; i < 8; i++) {
      int d = (head - i + 8) % 8;
      uint8_t v = d == 0 ? 255 : d == 1 ? 120 : d == 2 ? 50 : 12;
      strip.setPixelColor(i, urgent && (now / 250) % 2 ? col(v, 0, v / 3) : col(0, v / 4, v));
    }
  } else {
    // controllers on LEDs 1-7
    int shown = 0;
    for (int i = 0; i < MAX_PADS && shown < 7; i++) {
      const Pad &p = gip_pad(i);
      if (p.stage == PAD_FREE) continue;
      uint32_t c;
      if (p.stage != PAD_READY) c = col(200, 150, 0);
      else if (p.in.guide) c = col(255, 255, 255);
      else if (p.battKnown && p.battType == 1 && p.battLevel == 0 && (now / 500) % 2) c = col(220, 0, 0);
      else c = col(0, 200, 0);
      strip.setPixelColor(shown++, c);
    }
    if (!shown) {   // idle: slow amber breath on LED 1
      uint8_t v = 10 + (uint8_t)(20 * (1 + sinf(now / 600.0f)));
      strip.setPixelColor(0, col(v, v / 3, 0));
    }
    // USB link on LED 8
    uint32_t u = !hid_mounted() ? col(200, 0, 0) : hid_suspended() ? col(0, 60, 255) : col(0, 90, 0);
    strip.setPixelColor(7, u);
  }
  // per-LED overlays (connect/disconnect flashes)
  if (!pairing && hold <= 0.4f) for (int i = 0; i < 8; i++) if ((int32_t)(ovUntil[i] - now) > 0) strip.setPixelColor(i, ovColor[i]);
  strip.show();
}
