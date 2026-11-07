#include "settings.h"
#include <Preferences.h>

UiSettings cfg;

void settings_begin() {
  Preferences p;
  p.begin("xbui", true);
  cfg.brightness = p.getUChar("bright", cfg.brightness) % 5;
  cfg.ledLevel = p.getUChar("led", cfg.ledLevel) % 4;
  cfg.sleepMin = p.getUChar("sleep", cfg.sleepMin);
  cfg.accent = p.getUChar("accent", cfg.accent) % 4;
  cfg.reverseKnob = p.getBool("revknob", cfg.reverseKnob);
  cfg.autoDim = p.getUChar("dim", cfg.autoDim) % 4;
  cfg.flip = p.getBool("flip", cfg.flip);
  cfg.ringFx = p.getUChar("ringfx", cfg.ringFx) % 3;
  cfg.deadzone = p.getUChar("dz", cfg.deadzone) % 6;
  cfg.wakePc = p.getBool("wakepc", cfg.wakePc);
  cfg.pairWin = p.getUChar("pairwin", cfg.pairWin) % 4;
  cfg.pairBoot = p.getBool("pairboot", cfg.pairBoot);
  cfg.bleOn = p.getBool("bleon", cfg.bleOn);
  p.end();
}

void settings_save() {
  Preferences p;
  p.begin("xbui", false);
  p.putUChar("bright", cfg.brightness);
  p.putUChar("led", cfg.ledLevel);
  p.putUChar("sleep", cfg.sleepMin);
  p.putUChar("accent", cfg.accent);
  p.putBool("revknob", cfg.reverseKnob);
  p.putUChar("dim", cfg.autoDim);
  p.putBool("flip", cfg.flip);
  p.putUChar("ringfx", cfg.ringFx);
  p.putUChar("dz", cfg.deadzone);
  p.putBool("wakepc", cfg.wakePc);
  p.putUChar("pairwin", cfg.pairWin);
  p.putBool("pairboot", cfg.pairBoot);
  p.putBool("bleon", cfg.bleOn);
  p.end();
}

void settings_reset() { cfg = UiSettings(); settings_save(); }

uint32_t settings_pair_ms() { static const uint32_t ms[4] = {30000, 60000, 120000, 300000}; return ms[cfg.pairWin % 4]; }
uint32_t settings_dim_ms() { static const uint32_t ms[4] = {0, 30000, 60000, 300000}; return ms[cfg.autoDim % 4]; }
