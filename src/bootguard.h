#pragma once
#include "config.h"

// Keeps the board recoverable without opening it:
//  - crash-loop guard: 3 abnormal resets in a row -> reboot into the ROM bootloader
//  - boot_to_download(): software "enter download mode" (serial 'B', or hold the knob 10 s)
void bootguard_begin();   // call first thing in setup()
void bootguard_tick();    // call every loop
void boot_to_download();
void boot_mark(uint8_t step);          // call as the main loop advances; survives a watchdog reset
uint8_t boot_prev_mark();             // the step the previous run was in when it stopped
const char *boot_reason();   // why the chip last reset (poweron / brownout / panic / wdt / sw ...)
uint32_t boot_crash_count();
float boot_hold_progress();   // 0..1: how far the 10 s knob hold toward the bootloader has gone
