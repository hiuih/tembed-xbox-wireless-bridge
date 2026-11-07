#pragma once
#include "config.h"
#include "gip.h"
#ifdef XINPUT_MODE
// XInput must be the only USB function, so the log/console is the board's UART (the other USB-C port).
#define USBSerial Serial
#else
#include "USB.h"
#include "USBCDC.h"

// Serial log over the TinyUSB CDC port (started together with the gamepads in hid_begin()).
extern USBCDC USBSerial;
#endif

// USB HID gamepads: one top-level Gamepad collection (report id = slot+1) per slot.
void hid_begin();                       // call in setup(), before any USB traffic
void hid_task();                        // call often: sends changed reports
int hid_slots();
int hid_max_slots();                    // most gamepads/interfaces this build can expose                        // gamepads currently exposed to the host
void hid_request_slots(int n);          // persist new slot count (takes effect after re-plug/restart)
bool hid_mounted();
bool hid_suspended();
bool hid_wake_host();                   // USB remote wakeup (only if host allows it)
uint32_t hid_reports_sent();
uint32_t hid_rumble_msgs();
#ifdef XINPUT_MODE
void xinput_dump(char *buf, size_t n);
   // debug: what each XInput interface is being fed
#endif

// XInput descriptor style (0 = A real pad, 1 = B receiver style); a no-op in the plain HID builds.
int hid_pad_slot(int idx);                // controller slot behind output pad idx (connect order), -1 if none
int xinput_style();
void xinput_set_style(int st);            // changes style; the USB device re-enumerates, no restart
