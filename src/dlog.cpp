#include "dlog.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "esp_attr.h"
#include "esp_heap_caps.h"

#define DLOG_SZ 4096
#define DLOG_MAGIC 0xD106B007u
static RTC_NOINIT_ATTR char ring[DLOG_SZ];
static RTC_NOINIT_ATTR uint32_t head;      // total bytes ever written
static RTC_NOINIT_ATTR uint32_t magic;
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static volatile uint32_t lastRead = 0;

static void failedAlloc(size_t size, uint32_t caps, const char *fn) {
  dlog("ALLOC FAILED size=%u caps=0x%x in %s, internal free=%u largest=%u", (unsigned)size, (unsigned)caps, fn ? fn : "?",
       (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
}

void dlog_begin() {
  if (magic != DLOG_MAGIC) { memset(ring, 0, sizeof(ring)); head = 0; magic = DLOG_MAGIC; }
  heap_caps_register_failed_alloc_callback(failedAlloc);   // a memory failure is written to the log before anything else happens
}

static void put(const char *s, size_t n) {
  for (size_t i = 0; i < n; i++) ring[(head + i) % DLOG_SZ] = s[i];
  head += n;
}

void dlog(const char *fmt, ...) {
  char line[160];
  uint32_t ms = millis();
  int p = snprintf(line, sizeof(line), "[%5lu.%03lu] ", (unsigned long)(ms / 1000), (unsigned long)(ms % 1000));
  va_list ap; va_start(ap, fmt);
  int q = vsnprintf(line + p, sizeof(line) - p - 1, fmt, ap);
  va_end(ap);
  size_t n = p + (q < 0 ? 0 : q);
  if (n > sizeof(line) - 2) n = sizeof(line) - 2;
  line[n++] = '\n';
  portENTER_CRITICAL(&mux);
  put(line, n);
  portEXIT_CRITICAL(&mux);
}

uint32_t dlog_size() { return head < DLOG_SZ ? head : DLOG_SZ; }

size_t dlog_read(uint32_t offset, uint8_t *out, size_t n) {
  size_t got = 0;
  portENTER_CRITICAL(&mux);
  uint32_t size = head < DLOG_SZ ? head : DLOG_SZ;
  uint32_t start = head < DLOG_SZ ? 0 : head - DLOG_SZ;
  while (got < n && offset + got < size) { out[got] = ring[(start + offset + got) % DLOG_SZ]; got++; }
  portEXIT_CRITICAL(&mux);
  return got;
}

void dlog_note_read() { lastRead = millis() ? millis() : 1; }
uint32_t dlog_last_read_ms() { return lastRead; }
