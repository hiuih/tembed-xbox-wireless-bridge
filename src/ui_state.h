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

// Over-the-air update screen (see updater.cpp). The updater runs in a special boot mode with the Xbox radio off.
enum UpdState : uint8_t { U_MENU, U_SCAN, U_PASS, U_WPS, U_BUSY, U_READY, U_UPTODATE, U_DOWNLOAD, U_DONE, U_ERROR };
#define UPD_NETS 12
struct UpdView {
  UpdState state;
  char msg[64], detail[64];
  uint8_t progress;                // 0..100
  int nNets;
  char ssid[UPD_NETS][33];
  int8_t rssi[UPD_NETS];
  bool secure[UPD_NETS];
  char saved[33];                  // remembered network
  bool connected;
  char newVer[12];
  uint32_t size;
  uint32_t wpsLeftS;
  uint32_t doneLeftS;
};
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
  bool updMode;                    // running in update mode (no Xbox radio)
  UpdView upd;
  char kb[66]; int kbIdx, kbPage; bool kbShow;   // on-screen keyboard for the Wi-Fi password
  bool flip, wakePc, pairBoot, bleOn;
  uint8_t bleState;                // 0 off, 1 advertising, 2 connected
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
  SC_SPLASH, SC_HOME, SC_PADS, SC_PAD, SC_PAIR, SC_RADIO, SC_SPECTRUM, SC_POWER, SC_SETTINGS, SC_ABOUT, SC_LAG, SC_LOG, SC_UPDATE, SC_COUNT
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
