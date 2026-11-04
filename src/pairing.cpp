#include "pairing.h"
#include <Preferences.h>

static Preferences prefs;
static uint8_t known[MAX_PADS][6];
static int nKnown = 0;
static uint32_t windowUntil = 0;
static uint32_t flagUntil = 0;
static bool adoptFirst = true;   // until the first controller is paired, adopt whichever connects

static void save() {
  prefs.putUChar("n", nKnown);
  prefs.putBytes("macs", known, sizeof(known));
}

void pairing_begin() {
  prefs.begin("xbpairs", false);
  adoptFirst = prefs.getBool("adopt", true);
  nKnown = prefs.getUChar("n", 0);
  if (nKnown > MAX_PADS) nKnown = 0;
  memset(known, 0, sizeof(known));
  if (nKnown) prefs.getBytes("macs", known, sizeof(known));
}

int pairing_index(const uint8_t *mac) {
  for (int i = 0; i < nKnown; i++) if (!memcmp(known[i], mac, 6)) return i;
  return -1;
}
bool pairing_is_known(const uint8_t *mac) { return pairing_index(mac) >= 0; }
int pairing_count() { return nKnown; }
const uint8_t *pairing_mac(int idx) { return (idx >= 0 && idx < nKnown) ? known[idx] : nullptr; }

bool pairing_add(const uint8_t *mac) {
  if (pairing_is_known(mac)) return true;
  if (nKnown >= MAX_PADS) return false;
  memcpy(known[nKnown++], mac, 6);
  adoptFirst = false;
  prefs.putBool("adopt", false);
  save();
  return true;
}

void pairing_clear() {
  adoptFirst = false;
  prefs.putBool("adopt", false);
  nKnown = 0;
  memset(known, 0, sizeof(known));
  save();
}

bool pairing_remove(const uint8_t *mac) {
  int k = pairing_index(mac);
  if (k < 0) return false;
  for (int i = k; i < nKnown - 1; i++) memcpy(known[i], known[i + 1], 6);
  nKnown--;
  memset(known[nKnown], 0, 6);
  save();
  return true;
}

void pairing_start() { windowUntil = millis() + 60000; flagUntil = windowUntil; }
void pairing_stop() { windowUntil = 0; flagUntil = 0; }
bool pairing_active() { return windowUntil && (int32_t)(windowUntil - millis()) > 0; }
bool pairing_beacon_flag() { return flagUntil && (int32_t)(flagUntil - millis()) > 0; }
void pairing_paired_one() { flagUntil = 0; windowUntil = millis() + 20000; }
uint32_t pairing_ms_left() { return pairing_active() ? windowUntil - millis() : 0; }

bool pairing_allows(const uint8_t *mac) {
  // Known controllers always reconnect. Unknown ones only while pairing,
  // or when nothing has been paired yet (first controller is adopted).
  return pairing_is_known(mac) || pairing_active() || (adoptFirst && nKnown == 0);
}

void pairing_tick() {
  if (windowUntil && (int32_t)(millis() - windowUntil) >= 0) pairing_stop();
}
