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
  p.end();
}
