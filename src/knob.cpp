#include "knob.h"
#include "pairing.h"
#include "hid.h"
#include "leds.h"

void knob_tick() {
  static uint32_t downAt = 0;
  static bool done = false, done2 = false;
  uint32_t now = millis();
  if (digitalRead(PIN_ENC_KEY) == LOW) {
    if (!downAt) { downAt = now; done = false; done2 = false; }
    else if (!done && now - downAt >= 1000) {
      done = true;
      if (pairing_active()) pairing_stop(); else pairing_start();
    }
#ifdef XINPUT_MODE
    if (!done2 && now - downAt >= 4000) { done2 = true; leds_flash(170, 0, 255, 600); delay(600); xinput_cycle_mode(); }
#endif
  } else {
    downAt = 0;
  }
}
