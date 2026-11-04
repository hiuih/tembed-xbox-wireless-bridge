#include "knob.h"
#include "pairing.h"

void knob_tick() {
  static uint32_t downAt = 0;
  static bool done = false;
  uint32_t now = millis();
  if (digitalRead(PIN_ENC_KEY) == LOW) {
    if (!downAt) { downAt = now; done = false; }
    else if (!done && now - downAt >= 1000) {
      done = true;
      if (pairing_active()) pairing_stop(); else pairing_start();
    }
  } else {
    downAt = 0;
  }
}
