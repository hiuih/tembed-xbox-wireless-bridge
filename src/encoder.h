#pragma once
#include "config.h"

// Rotary knob decoder. Both A and B interrupt on every edge; each edge is looked up in a table of the four
// valid quadrature transitions, so contact bounce (which produces invalid or self-cancelling transitions)
// can never be read as a reversal. One physical click = 2 valid transitions on this encoder.
void encoder_begin();
int encoder_take();                     // net clicks since last call: + = clockwise (next/down), - = counter-clockwise
void encoder_set_reverse(bool reverse);
