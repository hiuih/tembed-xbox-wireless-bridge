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
  uint32_t gap[6];         // gaps between input reports: <10, <20, <40, <80, <160, >=160 ms
  uint32_t gipSeen, gipMissed;
};

struct PerfView {
  uint32_t evPerSec, rxPerSec, queueDrops, queueMax, loopAvgUs, loopMaxUs, heapFree, heapMin, txErr;
};

#define UI_HIST 60
#define UI_LOG_N 24
#define UI_LOG_LEN 30

static inline const char *usbModeName(int style) { return style ? "360 receiver" : "Wired 360 pad"; }
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
  uint8_t autoDim, ringFx, deadzone, pairWin;
  bool flip, wakePc, pairBoot;
  uint32_t pairTotalMs;            // length of the pairing window in use
  // event log, newest first
  int logN;
  char logMsg[UI_LOG_N][UI_LOG_LEN];
  uint32_t logAtS[UI_LOG_N];
  const char *fwVersion;
  const char *sleepWhy;            // why the previous run slept: - / hold / auto / menu
  uint8_t pmicFault;               // charger-chip fault bits (0 = none)
  const char *resetReason;         // why the chip last reset (shown on About)
  uint8_t prevMark, prevUiMark;    // where the main loop / UI task were when the previous run stopped
  uint16_t uiStackFree;            // UI task stack headroom, bytes
  uint8_t mac[6];
};

enum Screen : uint8_t {
  SC_SPLASH, SC_HOME, SC_PADS, SC_PAD, SC_PAIR, SC_RADIO, SC_SPECTRUM, SC_POWER, SC_SETTINGS, SC_ABOUT, SC_LAG, SC_LOG, SC_COUNT
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
