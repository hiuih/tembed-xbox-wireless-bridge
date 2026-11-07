#pragma once
#include "config.h"

// Screen + knob UI (T-Embed). Drawing lives in ui_screens*.cpp; this file is the hardware, navigation and data glue.
void ui_begin();                 // init display, knob, nRF24 and start the UI task
void ui_pump();                  // call from loop(): runs the actions the UI asked for on the protocol core
void ui_off();                   // backlight off (before deep sleep)
void ui_spi_lock();              // the screen, SD card and nRF24 share one SPI bus
void ui_spi_unlock();
