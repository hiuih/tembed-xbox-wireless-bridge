#pragma once
#include "config.h"
#include "radio.h"

struct PadInput {
  uint16_t buttons;   // raw GIP button bits (see GIP_BTN_*)
  uint16_t lt, rt;    // triggers 0..1023
  int16_t lx, ly, rx, ry;
  bool share, guide;
};

// GIP button bits
#define GIP_BTN_MENU   (1 << 2)
#define GIP_BTN_VIEW   (1 << 3)
#define GIP_BTN_A      (1 << 4)
#define GIP_BTN_B      (1 << 5)
#define GIP_BTN_X      (1 << 6)
#define GIP_BTN_Y      (1 << 7)
#define GIP_BTN_DUP    (1 << 8)
#define GIP_BTN_DDOWN  (1 << 9)
#define GIP_BTN_DLEFT  (1 << 10)
#define GIP_BTN_DRIGHT (1 << 11)
#define GIP_BTN_LB     (1 << 12)
#define GIP_BTN_RB     (1 << 13)
#define GIP_BTN_L3     (1 << 14)
#define GIP_BTN_R3     (1 << 15)

enum PadStage : uint8_t { PAD_FREE = 0, PAD_ASSOC, PAD_IDENT, PAD_READY };

struct Pad {
  PadStage stage;
  uint8_t mac[6];
  int8_t rssi;
  uint8_t channel;
  uint16_t vid, pid;
  uint8_t battType, battLevel;   // GIP battery type / level (0..3)
  bool battKnown;
  uint8_t battRaw;               // raw status byte from the controller (bit7 connected, bits3:2 type, bits1:0 level)
  uint32_t inHist[6];            // gap between input reports: <10, <20, <40, <80, <160, >=160 ms
  uint32_t gipSeen;              // GIP packets received (non-chunk)
  uint32_t inputGaps;            // input packets we never saw (GIP sequence gaps)
  uint32_t connectedAt, lastInputAt;
  uint32_t inputCount, assocCount;
  bool identified;                // controller completed the GIP identify handshake
  PadInput in;
};

enum PadEvent : uint8_t { PADEV_CONNECTED, PADEV_READY, PADEV_DISCONNECTED, PADEV_GUIDE_DOWN, PADEV_GUIDE_UP, PADEV_INPUT };
typedef void (*PadListener)(int slot, PadEvent ev);

void gip_begin(PadListener l);
void gip_handle(const RadioEvent &ev);   // feed radio events
void gip_tick();                          // timeouts, identify retries, pending rumble/LED
const Pad &gip_pad(int slot);
int gip_connected_count();

// Thread-safe request setters (applied from gip_tick on the protocol task).
void gip_rumble(int slot, uint8_t left, uint8_t right, uint8_t lt, uint8_t rt);  // 0..100
void gip_led(int slot, uint8_t mode, uint8_t brightness);                         // mode: 0 off,1 on,2..4 blink,8/9 fade
void gip_disconnect(int slot);
void gip_set_debug(bool on);
void gip_test_ack(int slot, int what);   // 0 rumble+ack, 1 rumble(internal)+ack, 2 LED+ack   // trace non-input GIP packets to the serial log
