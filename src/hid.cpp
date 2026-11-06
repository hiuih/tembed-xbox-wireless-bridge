#include "config.h"
#ifndef XINPUT_MODE
#include "hid.h"
#include "USB.h"
#include "USBHID.h"
#include <Preferences.h>
#include "pairing.h"

extern "C" bool tud_hid_n_ready(uint8_t instance);
extern "C" bool tud_hid_n_report(uint8_t instance, uint8_t report_id, void const *report, uint16_t len);
extern "C" bool tud_mounted(void);
extern "C" bool tud_suspended(void);
extern "C" bool tud_remote_wakeup(void);

#define DESC_PER_SLOT 112  // template is 109 bytes
USBCDC USBSerial;
static USBHID hid;
static uint8_t desc[MAX_PADS * DESC_PER_SLOT];
static uint16_t descLen = 0;
static int nSlots = 4;
static volatile uint32_t sent = 0, rumbleMsgs = 0;

struct __attribute__((packed)) Rep {
  uint16_t btn;
  uint8_t hat;
  int16_t lx, ly, rx, ry;
  uint16_t lt, rt;
};
static Rep last[MAX_PADS];
static bool lastValid[MAX_PADS];

static uint16_t appendSlot(uint8_t *d, uint8_t id) {
  static const uint8_t tpl[] = {
      0x05, 0x01, 0x09, 0x05, 0xA1, 0x01, 0x85, 0x00,                          // Gamepad collection, report id
      0x05, 0x09, 0x19, 0x01, 0x29, 0x10, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x10, 0x81, 0x02,  // 16 buttons
      0x05, 0x01, 0x09, 0x39, 0x15, 0x00, 0x25, 0x07, 0x35, 0x00, 0x46, 0x3B, 0x01, 0x65, 0x14,
      0x75, 0x04, 0x95, 0x01, 0x81, 0x42, 0x65, 0x00,                          // hat switch (null state)
      0x75, 0x04, 0x95, 0x01, 0x81, 0x03,                                      // padding nibble
      0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x33, 0x09, 0x34,              // X Y Rx Ry
      0x16, 0x00, 0x80, 0x26, 0xFF, 0x7F, 0x75, 0x10, 0x95, 0x04, 0x81, 0x02,
      0x05, 0x01, 0x09, 0x32, 0x09, 0x35,                                      // Z Rz (triggers)
      0x15, 0x00, 0x26, 0xFF, 0x03, 0x75, 0x10, 0x95, 0x02, 0x81, 0x02,
      0x06, 0x00, 0xFF, 0x09, 0x01, 0x15, 0x00, 0x26, 0xFF, 0x00,              // output: 4 rumble bytes
      0x75, 0x08, 0x95, 0x04, 0x91, 0x02,
      0xC0};
  memcpy(d, tpl, sizeof(tpl));
  d[7] = id;
  return sizeof(tpl);
}

class XboxHidDevice : public USBHIDDevice {
 public:
  uint16_t _onGetDescriptor(uint8_t *buf) override { memcpy(buf, desc, descLen); return descLen; }
  void _onOutput(uint8_t id, const uint8_t *b, uint16_t len) override {
    if (id < 1 || id > nSlots || len < 4) return;
    rumbleMsgs++;
    // bytes: strong(left), weak(right), left trigger, right trigger (0..255 -> 0..100)
    gip_rumble(id - 1, b[0] * 100 / 255, b[1] * 100 / 255, b[2] * 100 / 255, b[3] * 100 / 255);
  }
};
static XboxHidDevice dev;

void hid_begin() {
  Preferences p;
  p.begin("xbhid", true);
  int manual = p.getUChar("slots", 0);      // optional minimum set from the menu (0 = automatic)
  p.end();
  nSlots = manual;
  if (pairing_count() > nSlots) nSlots = pairing_count();   // automatic: one USB gamepad per paired controller
  if (nSlots < 1) nSlots = 1;
  if (nSlots > MAX_PADS) nSlots = MAX_PADS;
  descLen = 0;
  for (int i = 0; i < nSlots; i++) descLen += appendSlot(desc + descLen, i + 1);
  USBSerial.begin();
  USBSerial.setTxTimeoutMs(0);   // never block on a missing serial host
  hid.addDevice(&dev, descLen);
  hid.begin();
  USB.VID(0x303A);
  USB.PID(0x4010);
  USB.productName("Xbox Wireless Bridge");
  USB.manufacturerName("T-Embed");
  // Remote wakeup must be advertised in the configuration descriptor.
  USB.usbAttributes(TUSB_DESC_CONFIG_ATT_SELF_POWERED | TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP);
  USB.begin();
}

static uint8_t hatFrom(uint16_t b) {
  bool u = b & GIP_BTN_DUP, d = b & GIP_BTN_DDOWN, l = b & GIP_BTN_DLEFT, r = b & GIP_BTN_DRIGHT;
  if (u && r) return 1;
  if (d && r) return 3;
  if (d && l) return 5;
  if (u && l) return 7;
  if (u) return 0;
  if (r) return 2;
  if (d) return 4;
  if (l) return 6;
  return 8;
}

static void build(Rep &r, const Pad &p) {
  memset(&r, 0, sizeof(r));
  r.hat = 8;
  if (p.stage != PAD_READY) return;
  const PadInput &in = p.in;
  uint16_t b = in.buttons, o = 0;
  if (b & GIP_BTN_A) o |= 1 << 0;
  if (b & GIP_BTN_B) o |= 1 << 1;
  if (b & GIP_BTN_X) o |= 1 << 2;
  if (b & GIP_BTN_Y) o |= 1 << 3;
  if (b & GIP_BTN_LB) o |= 1 << 4;
  if (b & GIP_BTN_RB) o |= 1 << 5;
  if (b & GIP_BTN_VIEW) o |= 1 << 6;
  if (b & GIP_BTN_MENU) o |= 1 << 7;
  if (b & GIP_BTN_L3) o |= 1 << 8;
  if (b & GIP_BTN_R3) o |= 1 << 9;
  if (in.guide) o |= 1 << 10;
  if (in.share) o |= 1 << 11;
  if (b & GIP_BTN_DUP) o |= 1 << 12;      // D-pad also as plain buttons (matches the W3C standard layout 12-15)
  if (b & GIP_BTN_DDOWN) o |= 1 << 13;
  if (b & GIP_BTN_DLEFT) o |= 1 << 14;
  if (b & GIP_BTN_DRIGHT) o |= 1 << 15;
  r.btn = o;
  r.hat = hatFrom(b);
  r.lx = in.lx; r.ly = in.ly; r.rx = in.rx; r.ry = in.ry;
  r.lt = in.lt > 1023 ? 1023 : in.lt;
  r.rt = in.rt > 1023 ? 1023 : in.rt;
}

void hid_task() {
  if (!tud_mounted()) return;
  for (int i = 0; i < nSlots; i++) {
    Rep r;
    build(r, gip_pad(i));
    if (lastValid[i] && !memcmp(&r, &last[i], sizeof(r))) continue;
    if (!tud_hid_n_ready(0)) return;   // never block the protocol task
    if (tud_hid_n_report(0, i + 1, &r, sizeof(r))) { last[i] = r; lastValid[i] = true; sent++; }
  }
}

int hid_slots() { return nSlots; }
int hid_max_slots() { return MAX_PADS; }
void hid_request_slots(int n) {
  if (n < 1) n = 1;
  if (n > MAX_PADS) n = MAX_PADS;
  Preferences p;
  p.begin("xbhid", false);
  p.putUChar("slots", n);
  p.end();
}
bool hid_mounted() { return tud_mounted(); }
bool hid_suspended() { return tud_suspended(); }
bool hid_wake_host() { return tud_suspended() ? tud_remote_wakeup() : false; }
uint32_t hid_reports_sent() { return sent; }
uint32_t hid_rumble_msgs() { return rumbleMsgs; }
int xinput_style() { return 0; }
void xinput_set_style(int) {}
#endif
