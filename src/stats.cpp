#include "stats.h"
#include "radio.h"
#include "gip.h"
#if HAS_PMIC
#include "power.h"
#endif

static PerfView perf;
static uint32_t loopStartUs = 0, loopSumUs = 0, loopMaxUs = 0, loopN = 0, events = 0;
static uint32_t lastTickMs = 0, lastRx = 0, lastInputCount[MAX_PADS];
static uint16_t padRate[MAX_PADS];
static uint16_t rateHist[2][UI_HIST], loopHist[UI_HIST], vbatHist[UI_HIST];
static uint8_t histFill = 0, vbatDiv = 0;

void stats_loop_begin() { loopStartUs = micros(); }

void stats_loop_end() {
  uint32_t d = micros() - loopStartUs;
  loopSumUs += d; loopN++;
  if (d > loopMaxUs) loopMaxUs = d;
}

void stats_event() { events++; }

static void push(uint16_t *h, uint16_t v) {
  memmove(h, h + 1, (UI_HIST - 1) * sizeof(uint16_t));
  h[UI_HIST - 1] = v;
}

void stats_tick() {
  uint32_t now = millis();
  if (now - lastTickMs < 1000) return;
  lastTickMs = now;
  const RadioStats &r = radio_stats();
  perf.rxPerSec = r.rxFrames - lastRx; lastRx = r.rxFrames;
  perf.evPerSec = events; events = 0;
  perf.queueDrops = r.queueDrops;
  perf.queueMax = radio_queue_max();
  perf.loopAvgUs = loopN ? loopSumUs / loopN : 0;
  perf.loopMaxUs = loopMaxUs;
  perf.txErr = r.txErr;
  perf.heapFree = ESP.getFreeHeap();
  perf.heapMin = ESP.getMinFreeHeap();
  loopSumUs = loopMaxUs = loopN = 0;

  // per-pad input rate; the two busiest online pads feed the graph
  uint16_t best[2] = {0, 0};
  int n = 0;
  for (int i = 0; i < MAX_PADS; i++) {
    const Pad &p = gip_pad(i);
    uint32_t c = p.inputCount;
    padRate[i] = (p.stage == PAD_READY && c >= lastInputCount[i]) ? c - lastInputCount[i] : 0;
    lastInputCount[i] = c;
    if (p.stage == PAD_READY && n < 2) best[n++] = padRate[i];
  }
  push(rateHist[0], best[0]);
  push(rateHist[1], best[1]);
  push(loopHist, perf.loopMaxUs > 65000 ? 65000 : perf.loopMaxUs);
  if (++vbatDiv >= 5) {                         // battery voltage every 5 s = 5 minutes of history
    vbatDiv = 0;
#if HAS_PMIC
    PowerInfo pi;
    if (power_read(pi)) push(vbatHist, (uint16_t)(pi.vbat * 1000));
#endif
  }
  if (histFill < UI_HIST) histFill++;
}

const PerfView &stats_perf() { return perf; }
uint16_t stats_pad_rate(int slot) { return padRate[slot]; }

void stats_fill(Snap &s) {
  s.perf = perf;
  memcpy(s.rateHist, rateHist, sizeof(rateHist));
  memcpy(s.loopHist, loopHist, sizeof(loopHist));
  memcpy(s.vbatHist, vbatHist, sizeof(vbatHist));
  s.histFill = histFill;
}
