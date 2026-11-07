#include "evlog.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static char buf[EVLOG_N][EVLOG_LEN];
static uint32_t atS[EVLOG_N];
static uint32_t total = 0;
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

void evlog_add(const char *fmt, ...) {
  char tmp[EVLOG_LEN];
  va_list ap; va_start(ap, fmt); vsnprintf(tmp, sizeof(tmp), fmt, ap); va_end(ap);
  portENTER_CRITICAL(&mux);
  int i = total % EVLOG_N;
  memcpy(buf[i], tmp, EVLOG_LEN);
  atS[i] = millis() / 1000;
  total++;
  portEXIT_CRITICAL(&mux);
}

int evlog_copy(char msg[][EVLOG_LEN], uint32_t *at) {
  portENTER_CRITICAL(&mux);
  int n = total < EVLOG_N ? (int)total : EVLOG_N;
  for (int k = 0; k < n; k++) {
    int i = (total - 1 - k) % EVLOG_N;
    memcpy(msg[k], buf[i], EVLOG_LEN);
    at[k] = atS[i];
  }
  portEXIT_CRITICAL(&mux);
  return n;
}

uint32_t evlog_total() { return total; }
