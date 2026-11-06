#pragma once
#include "config.h"
#include "gip.h"

// 8-LED ring around the knob = the status display (the screen is unreliable).
//  LEDs 1-7 : one per controller. green=ready yellow=connecting red/green blink=low battery white=Xbox button held
//  LED 8    : USB link to the computer. green=connected blue=computer asleep red=not connected
//  spinning blue = pairing (red-blue pulse in last 10 s)   green flash = paired   amber x2 = pairing timed out
//  white pulse = woke the computer   purple = restarting   orange pulse = rumble test   red fill = bootloader hold
void leds_begin();
void leds_tick();
void leds_off();
void leds_set_level(uint8_t level);   // 0 off, 1 low, 2 medium, 3 high           // all dark (used before powering off)
void leds_flash(uint8_t r, uint8_t g, uint8_t b, uint16_t ms);   // whole ring
void leds_pad_event(int slot, PadEvent ev);                       // per-controller flashes
