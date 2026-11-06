#pragma once
#include "config.h"

// Hold the knob for 1 s: toggle pairing mode (no screen needed). Clicks are handled by the UI.
void knob_tick();

bool knob_take_click();      // a short press of the knob (released before 1 s)
