#pragma once
#include "config.h"

// Persistent debug log. It lives in RTC memory, so it survives crashes, watchdog resets and deep sleep (it is lost
// only when power is fully removed). It is read over USB while the board is plugged in (see tools/devlog.py):
// a vendor control request on the XInput interface, so the gamepad itself is unaffected.
void dlog_begin();                                                  // call first thing in setup()
void dlog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
size_t dlog_read(uint32_t offset, uint8_t *out, size_t n);          // bytes of the log, oldest first
uint32_t dlog_size();
void dlog_note_read();                                              // a reader is attached
uint32_t dlog_last_read_ms();
void dlog_install_crash_capture();   // asserts, aborts and CPU exceptions are written to the log before the reboot
