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

// Crash capture (only where the build enables it with DLOG_CRASH_CAPTURE and the matching --wrap link flags).
// The panic handler prints to a UART that is not reachable on this board, so instead the text of an assert / abort /
// ESP_ERROR_CHECK failure and the registers + call chain of a CPU exception are written to this persistent log first.
#ifdef DLOG_CRASH_CAPTURE
#include "esp_debug_helpers.h"
#include "esp_err.h"
#include "freertos/xtensa_api.h"
#include "xtensa/xtensa_context.h"

static void logChain(const char *tag, uint32_t pc, uint32_t sp, uint32_t nextPc) {
  esp_backtrace_frame_t f = {pc, sp, nextPc};
  char line[150]; int n = 0;
  n += snprintf(line + n, sizeof(line) - n, "%s chain:", tag);
  for (int i = 0; i < 14; i++) {
    n += snprintf(line + n, sizeof(line) - n, " %08x", (unsigned)f.pc);
    if (!esp_backtrace_get_next_frame(&f)) break;
    if (n > (int)sizeof(line) - 12) break;
  }
  dlog("%s", line);
}

extern "C" void __real_abort(void);
extern "C" void __wrap_abort(void) {
  uint32_t pc, sp, next;
  esp_backtrace_get_start(&pc, &sp, &next);
  logChain("ABORT", pc, sp, next);
  __real_abort();
}

extern "C" void __real___assert_func(const char *, int, const char *, const char *);
extern "C" void __wrap___assert_func(const char *file, int line, const char *func, const char *expr) {
  dlog("ASSERT failed: %s:%d %s: %s", file ? file : "?", line, func ? func : "?", expr ? expr : "?");
  __real___assert_func(file, line, func, expr);
}

extern "C" void __real__esp_error_check_failed(esp_err_t, const char *, int, const char *, const char *);
extern "C" void __wrap__esp_error_check_failed(esp_err_t rc, const char *file, int line, const char *function, const char *expression) {
  dlog("ESP_ERROR_CHECK failed: rc=0x%x %s:%d %s: %s", (unsigned)rc, file ? file : "?", line, function ? function : "?", expression ? expression : "?");
  __real__esp_error_check_failed(rc, file, line, function, expression);
}

static void excHandler(XtExcFrame *ef) {
  dlog("EXCEPTION cause=%u pc=%08x a0=%08x sp=%08x excvaddr=%08x", (unsigned)ef->exccause, (unsigned)ef->pc, (unsigned)ef->a0, (unsigned)ef->a1, (unsigned)ef->excvaddr);
  logChain("EXC", ef->pc, ef->a1, ef->a0);
  abort();
}

void dlog_install_crash_capture() {
  static const int causes[] = {0, 2, 3, 6, 9, 12, 13, 14, 15, 20, 28, 29};
  for (int c : causes) xt_set_exception_handler(c, (xt_exc_handler)excHandler);
}
#else
void dlog_install_crash_capture() {}
#endif
