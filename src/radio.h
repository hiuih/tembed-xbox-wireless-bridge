#pragma once
#include "config.h"

enum RadioEventKind : uint8_t {
  EV_ASSOC = 1,    // association / reassociation request
  EV_PAIRREQ = 2,  // vendor pair request (70 01)
  EV_GIP = 3,      // QoS-data frame carrying one or more GIP packets
  EV_NULL = 4,     // keepalive from a controller we have something waiting to send to (it is awake right now)
};

struct RadioEvent {
  uint8_t kind;
  uint8_t mac[6];  // transmitter (controller)
  uint8_t channel;
  int8_t rssi;
  uint16_t len;
  uint8_t data[236];
};

void radio_begin();
void radio_tick();                       // beacon + channel scan; call often
bool radio_poll(RadioEvent &ev);         // pop one RX event (non-blocking)

// TX helpers. All must be called from the main (protocol) task only.
bool radio_send_qos(const uint8_t *mac, const uint8_t *gip, int len);
bool radio_send_mgmt(uint8_t fc0, const uint8_t *mac, const uint8_t *body, int len);

// Liveness tracking: register a controller MAC in a slot so the RX callback can
// timestamp its traffic and drop duplicate (retried) data frames.
void radio_register(int slot, const uint8_t *mac);  // mac==nullptr unregisters
uint32_t radio_last_rx(int slot);
void radio_set_pending_tx(int slot, bool on);   // ask for an EV_NULL the next time this controller transmits
uint32_t radio_lost_frames(int slot);   // 802.11 sequence gaps seen from this controller (frames our radio missed)
uint32_t radio_rx_frames(int slot);

void radio_set_pairing(bool on);         // beacon pair flag
bool radio_pairing();
uint8_t radio_channel();
bool radio_scanning();                   // true while hopping (no clients)

struct RadioStats {
  uint32_t rxFrames, toMe, dupDropped, queueDrops, txErr;
};
const RadioStats &radio_stats();
