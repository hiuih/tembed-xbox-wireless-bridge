#pragma once
#include "config.h"

struct UiSettings {
  uint8_t brightness = 3;    // 0..4 -> 20..100 %
  uint8_t ledLevel = 2;      // 0 off, 1 low, 2 medium, 3 high
  uint8_t sleepMin = 15;     // auto-sleep after this many idle minutes with no controller (0 = never)
  uint8_t accent = 0;        // 0 green, 1 blue, 2 purple, 3 orange
  bool reverseKnob = false;
  uint8_t autoDim = 2;       // 0 never, 1 30 s, 2 1 min, 3 5 min (screen dims when idle with no controller)
  bool flip = false;         // screen rotated 180 degrees
  uint8_t ringFx = 0;        // idle ring effect: 0 amber breath, 1 accent breath, 2 rainbow
  uint8_t deadzone = 0;      // stick deadzone, steps of 5 % (0..5)
  bool wakePc = true;        // Xbox button / controller power-on wakes a sleeping computer
  uint8_t pairWin = 1;       // pairing window: 0 30 s, 1 1 min, 2 2 min, 3 5 min
  bool pairBoot = false;     // open the pairing window at power-up
};
extern UiSettings cfg;
void settings_begin();
void settings_save();
void settings_reset();
uint32_t settings_pair_ms();     // length of the pairing window
uint32_t settings_dim_ms();      // 0 = never dim
