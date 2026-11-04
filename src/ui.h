#pragma once
#include "config.h"

// Screen + rotary-encoder UI: controller list, live view, pairing and settings.
void ui_begin();                 // init display/encoder and start the UI task
void ui_notify_pairing_done();   // optional hook: a controller just paired
void ui_off();                 // backlight off (before powering off)
