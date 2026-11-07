#pragma once
#include "config.h"

// Small in-RAM event log shown on the Log screen. The first character of a message is its colour class:
// '+' good, '-' bad, '!' warning, anything else neutral. Safe to call from either core.
#define EVLOG_N 24
#define EVLOG_LEN 30
void evlog_add(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
// Copies up to EVLOG_N entries, newest first. Returns how many.
int evlog_copy(char msg[][EVLOG_LEN], uint32_t *atS);
uint32_t evlog_total();
