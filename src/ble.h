#pragma once
#include "config.h"

// Bluetooth LE gamepads: the Xbox controllers (connected over their own link) are re-exported to a computer as BLE HID
// gamepads, one top-level Gamepad collection per pad in a single Bluetooth device. Optional (Settings > Bluetooth).
// The ESP32-S3 has Bluetooth LE only (no Bluetooth Classic), and it shares the 2.4 GHz radio with the Xbox link.
#define BLE_PADS 4
enum { BLE_OFF = 0, BLE_ADVERTISING = 1, BLE_CONNECTED = 2 };

#ifdef BLE_ENABLED
void ble_begin(bool on);          // call once in setup(); does nothing when off (and costs no memory then)
void ble_tick();                  // call often: sends changed reports
int ble_state();
bool ble_forget_hosts();          // delete all Bluetooth bonds; false if Bluetooth is off
#else
static inline void ble_begin(bool) {}
static inline void ble_tick() {}
static inline int ble_state() { return BLE_OFF; }
static inline bool ble_forget_hosts() { return false; }
#endif
