#pragma once
#include "config.h"
#include "gip.h"
#include "USB.h"
#include "USBCDC.h"

// Serial log over the TinyUSB CDC port (started together with the gamepads in hid_begin()).
extern USBCDC USBSerial;

// USB HID gamepads: one top-level Gamepad collection (report id = slot+1) per slot.
void hid_begin();                       // call in setup(), before any USB traffic
void hid_task();                        // call often: sends changed reports
int hid_slots();                        // gamepads currently exposed to the host
void hid_request_slots(int n);          // persist new slot count (takes effect after re-plug/restart)
bool hid_mounted();
bool hid_suspended();
bool hid_wake_host();                   // USB remote wakeup (only if host allows it)
uint32_t hid_reports_sent();
uint32_t hid_rumble_msgs();
