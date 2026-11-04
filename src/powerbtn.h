#pragma once
#include "config.h"

// T-Embed only. Hold the user key (GPIO6, the separate button, not the knob) for 5 s to power the board off:
//  - on battery alone: real shutdown through the charger chip (wake with the PWR button)
//  - on USB power: the charger chip cannot cut power, so go to deep sleep instead (wake with the same key)
void powerbtn_begin();          // call early in setup(): releases pin holds after waking from deep sleep
void powerbtn_tick();
float power_hold_progress();    // 0..1 while the key is held (drives the LED countdown)
