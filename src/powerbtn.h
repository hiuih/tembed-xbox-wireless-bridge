#pragma once
#include "config.h"

// T-Embed only. Hold the user key (GPIO6, the separate button, not the knob) for 5 s to put the board into deep sleep;
// press the same key to wake it. The battery is never disconnected (an earlier build cut it with the charger chip and
// the board then could not be woken without unplugging the battery).
void powerbtn_begin();          // call early in setup(): releases pin holds after waking from deep sleep
void powerbtn_tick();
float power_hold_progress();
bool powerbtn_take_back();      // short press of the top key = back
enum { SLEEP_HOLD = 1, SLEEP_AUTO = 2, SLEEP_MENU = 3 };
const char *powerbtn_last_sleep();   // why the previous run went to sleep ("-" if it did not)
void powerbtn_sleep_now(int why = SLEEP_MENU);      // deep sleep (wakes on the top key)    // 0..1 while the key is held (drives the LED countdown)
