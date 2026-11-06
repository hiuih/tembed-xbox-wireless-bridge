#pragma once
#include "config.h"

struct UiSettings {
  uint8_t brightness = 3;    // 0..4 -> 20..100 %
  uint8_t ledLevel = 2;      // 0 off, 1 low, 2 medium, 3 high
  uint8_t sleepMin = 15;     // auto-sleep after this many idle minutes with no controller (0 = never)
  uint8_t accent = 0;        // 0 green, 1 blue, 2 purple, 3 orange
  bool reverseKnob = false;
};
extern UiSettings cfg;
void settings_begin();
void settings_save();
