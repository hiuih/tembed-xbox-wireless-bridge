#pragma once
#include "config.h"
#include "ui_state.h"

// Loop / radio load counters, sampled once a second, plus short histories for the graphs.
void stats_loop_begin();                // call at the top of loop()
void stats_loop_end();                  // call at the bottom of loop()
void stats_event();                     // one radio event processed
void stats_tick();                      // call every loop; does its work once per second
const PerfView &stats_perf();
void stats_fill(Snap &s);               // copies histories and per-pad rates into a snapshot
uint16_t stats_pad_rate(int slot);
