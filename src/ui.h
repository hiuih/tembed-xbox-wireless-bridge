#pragma once
#include "config.h"

// Screen + knob UI (T-Embed). Drawing lives in ui_screens*.cpp; this file is the hardware, navigation and data glue.
void ui_begin();                 // init display, knob, nRF24 and start the UI task
void ui_off();                   // backlight off (before deep sleep)
