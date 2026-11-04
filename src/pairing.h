#pragma once
#include "config.h"

// Persistent list of paired controllers (NVS) and the pairing window.
void pairing_begin();
bool pairing_is_known(const uint8_t *mac);
int pairing_index(const uint8_t *mac);          // index in known list, -1 if unknown
bool pairing_add(const uint8_t *mac);           // false if list full
int pairing_count();
void pairing_clear();                           // forget everyone (also disables auto-adopt)
bool pairing_remove(const uint8_t *mac);        // forget one controller
const uint8_t *pairing_mac(int idx);

// Pairing window: open for 60 s, sets the beacon pair flag.
void pairing_start(uint32_t ms = 60000);
void pairing_stop();
bool pairing_active();                          // window open (accept new controllers)
bool pairing_beacon_flag();                     // pair flag to advertise
void pairing_paired_one();                      // call after PAIR_RESP: drop flag, short grace window
uint32_t pairing_ms_left();
bool pairing_allows(const uint8_t *mac);        // may this MAC associate right now?
void pairing_tick();
