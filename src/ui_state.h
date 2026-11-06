#pragma once
// Everything the screens need, as one plain snapshot. The firmware fills it from live modules; the Mac-side
// simulator (tools/sim) fills it with fake data so the screens can be rendered to images and reviewed.
#include "config.h"
#include "gip.h"

struct PadView {
  bool present;            // slot in use (connected or paired)
  bool online;             // controller is connected and ready
  bool connecting;
  char name[16];
  int8_t rssi;
  bool battKnown;
  uint8_t battType, battLevel;   // GIP battery type / level 0..3
  PadInput in;
  uint16_t rateHz;         // input reports per second
  uint16_t lossPermille;   // missing GIP packets, per mille
  uint32_t upS;
  // short stick history for the trail effect (normalised -100..100)
  int8_t trail[8][4];      // [age][lx, ly, rx, ry]
};

struct PerfView {
  uint32_t evPerSec, rxPerSec, queueDrops, queueMax, loopAvgUs, loopMaxUs, heapFree, heapMin, txErr;
};

#define UI_HIST 60
struct Snap {
  uint32_t nowMs;
  uint32_t uptimeS;
  bool usbMounted, usbSuspended;
  int gamepads;            // XInput interfaces currently exposed to the PC
  int gamepadsMax;
  int style;               // XInput descriptor style 0 = A, 1 = B
  PadView pad[MAX_PADS];
  int nReady, nKnown;
  bool pairing;
  uint32_t pairLeftMs;
  uint8_t channel;
  bool scanning;
  // power
  bool pmicOk, vbusPresent, charging;
  int battPct;
  float vbat, vbus, vsys;
  int chgmA;
  PerfView perf;
  uint16_t rateHist[2][UI_HIST];   // input Hz of the two busiest pads, 1 sample/s, newest last
  uint16_t loopHist[UI_HIST];      // worst loop time per second, microseconds
  uint16_t vbatHist[UI_HIST];      // battery millivolts, newest last
  uint8_t histFill;
  // 2.4 GHz spectrum (nRF24)
  bool nrfOk;
  uint8_t spec[84];                // occupancy 0..100 %
  uint8_t peak[84];
  // settings shown on screen
  uint8_t brightness, ledLevel, sleepMin, accent;
  bool reverseKnob;
  const char *fwVersion;
  uint8_t mac[6];
};

enum Screen : uint8_t {
  SC_SPLASH, SC_HOME, SC_PADS, SC_PAD, SC_PAIR, SC_RADIO, SC_SPECTRUM, SC_POWER, SC_SETTINGS, SC_ABOUT, SC_COUNT
};

struct Nav {
  Screen screen;
  int sel;                 // selected item on the current screen
  int scroll;              // first visible row on list screens
  int padSlot;             // controller shown on SC_PAD
  float selX, selY;        // animated selector position (home grid)
  float scrollAnim;        // animated list offset
  uint32_t screenSinceMs;  // for transition effects
  uint32_t toastUntilMs;
  char toast[40];
  bool confirm;            // two-step confirmation armed (settings)
};
