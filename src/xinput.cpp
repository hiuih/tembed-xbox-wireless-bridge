// XInput (Xbox 360 wired controller) USB device for Windows.
//
// Windows treats a device as a real Xbox controller (XInput: games show Xbox glyphs and use the Xbox mapping)
// when it exposes the Xbox 360 vendor interface: class 0xFF, subclass 0x5D, protocol 0x01, with one interrupt IN
// and one interrupt OUT endpoint. This implements that on top of TinyUSB's application class driver hook.
// XInput must be the ONLY USB function, so there is no USB serial here: logs go to the UART port instead.
//
// One interface = one controller (XInput player slot). Several controllers = several such interfaces in one device.
#include "config.h"
#ifdef XINPUT_MODE
#include "hid.h"
#include "gip.h"
#include <Preferences.h>
#include "pairing.h"
#include "USB.h"
extern "C" {
#include "tusb.h"
#include "device/usbd_pvt.h"
#include "esp32-hal-tinyusb.h"
}

// One interface = one XInput controller. A real Xbox 360 pad is a single interface, which is the proven case for
// Windows' driver; more than one is experimental (build env s3_xinput2).
#ifndef XINPUT_INTERFACES
#define XINPUT_INTERFACES 1
#endif
// Layouts (selected at runtime, stored in flash, cycled by holding BOOT for 4 s):
//   mode 0: ONE Xbox controller interface, real-pad class descriptor  (known to work on Windows)
//   mode 1: TWO interfaces, real-pad class descriptor                  (experimental)
//   mode 2: TWO interfaces, receiver-style class descriptor            (experimental; layout from the multi-controller
//           emulation in github.com/Berghopper/360-w-raw-gadget, MIT)
#define XI_MAX_IF 2

static int nSlots = 1;
static int mode = 0;
static int unkLen = 17;
static uint8_t epIn[XI_MAX_IF], epOut[XI_MAX_IF];
static bool opened[XI_MAX_IF];
static uint8_t outBuf[XI_MAX_IF][32];
static uint8_t inBuf[XI_MAX_IF][20];
static uint8_t lastRep[XI_MAX_IF][20];
static bool lastValid[XI_MAX_IF];
static uint32_t lastSent[XI_MAX_IF];
static volatile uint32_t sent = 0, rumbleMsgs = 0;
static volatile int8_t ifToSlot[XI_MAX_IF];   // XInput interface k carries controller slot ifToSlot[k] (-1 = none)

// Like a real console: the controller that connected first is player 1 (interface 0), the next is player 2, ...
static void remap() {
  int8_t order[MAX_PADS]; int n = 0;
  for (int i = 0; i < MAX_PADS; i++) if (gip_pad(i).stage == PAD_READY) order[n++] = i;
  for (int a = 1; a < n; a++) {            // insertion sort by connect time
    int8_t v = order[a]; int b = a - 1;
    while (b >= 0 && gip_pad(order[b]).connectedAt > gip_pad(v).connectedAt) { order[b + 1] = order[b]; b--; }
    order[b + 1] = v;
  }
  for (int k = 0; k < XI_MAX_IF; k++) ifToSlot[k] = k < n ? order[k] : -1;
}

// ---------------------------------------------------------------- descriptors
static uint16_t xinputDescriptors(uint8_t *dst, uint8_t *itf) {
  uint8_t *p = dst;
  for (int i = 0; i < nSlots; i++) {
    uint8_t ep = tinyusb_get_free_duplex_endpoint();      // same endpoint number for IN and OUT, like a real pad
    uint8_t in = 0x80 | ep, out = ep;
    uint8_t n = (*itf)++;
    const uint8_t ifd[9] = {9, TUSB_DESC_INTERFACE, n, 0, 2, 0xFF, 0x5D, 0x01, 0};
    memcpy(p, ifd, 9); p += 9;
    if (mode == 2) {
      const uint8_t unk[20] = {0x14, 0x22, 0x00, 0x01, 0x13, in, 0x1d, 0x00, 0x17, 0x01, 0x02, 0x08, 0x13, out, 0x0c, 0x00, 0x0c, 0x01, 0x02, 0x08};
      memcpy(p, unk, sizeof(unk)); p += sizeof(unk);
    } else {
      const uint8_t unk[17] = {0x11, 0x21, 0x00, 0x01, 0x01, 0x25, in, 0x14, 0, 0, 0, 0, 0x13, out, 0x08, 0, 0};
      memcpy(p, unk, sizeof(unk)); p += sizeof(unk);
    }
    const uint8_t epi[7] = {7, TUSB_DESC_ENDPOINT, in, TUSB_XFER_INTERRUPT, 0x20, 0x00, 4};
    memcpy(p, epi, 7); p += 7;
    const uint8_t epo[7] = {7, TUSB_DESC_ENDPOINT, out, TUSB_XFER_INTERRUPT, 0x20, 0x00, 8};
    memcpy(p, epo, 7); p += 7;
  }
  return p - dst;
}

// ---------------------------------------------------------------- class driver
static void drvInit(void) {}
static void drvReset(uint8_t) { for (int i = 0; i < XI_MAX_IF; i++) { opened[i] = false; lastValid[i] = false; } }

static uint16_t drvOpen(uint8_t rhport, tusb_desc_interface_t const *itf, uint16_t max_len) {
  if (itf->bInterfaceClass != 0xFF || itf->bInterfaceSubClass != 0x5D || itf->bInterfaceProtocol != 0x01) return 0;
  uint8_t idx = itf->bInterfaceNumber;
  if (idx >= XI_MAX_IF) return 0;
  uint8_t const *p = (uint8_t const *)itf;
  p = tu_desc_next(p);   // class-specific descriptor (0x21 or 0x22)
  uint16_t len = sizeof(tusb_desc_interface_t) + p[0] + 2 * sizeof(tusb_desc_endpoint_t);
  if (max_len < len) return 0;
  p = tu_desc_next(p);   // first endpoint
  if (!usbd_open_edpt_pair(rhport, p, 2, TUSB_XFER_INTERRUPT, &epOut[idx], &epIn[idx])) return 0;
  opened[idx] = true;
  usbd_edpt_xfer(rhport, epOut[idx], outBuf[idx], sizeof(outBuf[idx]));   // listen for rumble / LED reports
  return len;
}

static bool drvControl(uint8_t rhport, uint8_t stage, tusb_control_request_t const *req) {
  if (stage == CONTROL_STAGE_SETUP) {
    if (req->wLength == 0) return tud_control_status(rhport, req);
    return false;   // unknown data request: stall
  }
  return true;
}

static bool drvXfer(uint8_t rhport, uint8_t ep, xfer_result_t result, uint32_t n) {
  for (int i = 0; i < XI_MAX_IF; i++) {
    if (!opened[i]) continue;
    if (ep == epOut[i]) {
      if (result == XFER_RESULT_SUCCESS && n >= 5 && outBuf[i][0] == 0x00 && outBuf[i][1] == 0x08) {
        rumbleMsgs++;
        // 00 08 00 <left/strong> <right/weak> ...  (0..255) -> GIP motor strength (0..100)
        if (ifToSlot[i] >= 0) gip_rumble(ifToSlot[i], outBuf[i][3] * 100 / 255, outBuf[i][4] * 100 / 255, 0, 0);
      }
      usbd_edpt_xfer(rhport, epOut[i], outBuf[i], sizeof(outBuf[i]));   // re-arm
      return true;
    }
    if (ep == epIn[i]) return true;
  }
  return false;
}

static const usbd_class_driver_t xinputDriver = {drvInit, drvReset, drvOpen, drvControl, drvXfer, NULL};

extern "C" usbd_class_driver_t const *usbd_app_driver_get_cb(uint8_t *driver_count) {
  *driver_count = 1;
  return &xinputDriver;
}

// ---------------------------------------------------------------- reports
static void build(uint8_t *r, const Pad &p) {
  memset(r, 0, 20);
  r[1] = 0x14;
  if (p.stage != PAD_READY) return;
  const PadInput &in = p.in;
  uint16_t b = in.buttons;
  uint8_t lo = 0, hi = 0;
  if (b & GIP_BTN_DUP) lo |= 0x01;
  if (b & GIP_BTN_DDOWN) lo |= 0x02;
  if (b & GIP_BTN_DLEFT) lo |= 0x04;
  if (b & GIP_BTN_DRIGHT) lo |= 0x08;
  if (b & GIP_BTN_MENU) lo |= 0x10;     // Start
  if (b & GIP_BTN_VIEW) lo |= 0x20;     // Back
  if (b & GIP_BTN_L3) lo |= 0x40;
  if (b & GIP_BTN_R3) lo |= 0x80;
  if (b & GIP_BTN_LB) hi |= 0x01;
  if (b & GIP_BTN_RB) hi |= 0x02;
  if (in.guide) hi |= 0x04;
  if (b & GIP_BTN_A) hi |= 0x10;
  if (b & GIP_BTN_B) hi |= 0x20;
  if (b & GIP_BTN_X) hi |= 0x40;
  if (b & GIP_BTN_Y) hi |= 0x80;
  r[2] = lo; r[3] = hi;
  r[4] = (in.lt > 1023 ? 1023 : in.lt) >> 2;
  r[5] = (in.rt > 1023 ? 1023 : in.rt) >> 2;
  // GIP and XInput both use "up = positive" on Y; PadInput stores Y inverted for HID, so undo that here.
  int16_t v[4] = {in.lx, (int16_t)~in.ly, in.rx, (int16_t)~in.ry};
  for (int i = 0; i < 4; i++) { r[6 + 2 * i] = v[i] & 0xff; r[7 + 2 * i] = (v[i] >> 8) & 0xff; }
}

void hid_begin() {
  Serial.begin(115200);
  Preferences pr;
  pr.begin("xbhid", true);
  mode = pr.getUChar("ximode", 0);
  pr.end();
  if (mode < 0 || mode > 2) mode = 0;
  nSlots = mode == 0 ? 1 : XI_MAX_IF;
  unkLen = mode == 2 ? 20 : 17;
  tinyusb_enable_interface(USB_INTERFACE_CUSTOM, nSlots * (9 + unkLen + 14), xinputDescriptors);
  USB.VID(0x045E);
  USB.PID(0x028E);
  USB.usbClass(0xFF);
  USB.usbSubClass(0xFF);
  USB.usbProtocol(0xFF);
  USB.usbVersion(0x0200);
  USB.firmwareVersion(0x0114);
  USB.productName("Controller");
  USB.manufacturerName("Microsoft");
  USB.usbAttributes(TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP);   // 0xA0, like a real wired 360 pad (bus powered, 500 mA)
  USB.begin();
}

int xinput_mode() { return mode; }
void xinput_cycle_mode() {
  Preferences pr;
  pr.begin("xbhid", false);
  pr.putUChar("ximode", (mode + 1) % 3);
  pr.end();
  ESP.restart();
}

void hid_task() {
  if (!tud_mounted()) return;
  uint32_t now = millis();
  remap();
  for (int i = 0; i < nSlots; i++) {
    if (!opened[i]) continue;
    uint8_t *r = inBuf[i];
    uint8_t cur[20];
    if (ifToSlot[i] >= 0) build(cur, gip_pad(ifToSlot[i])); else { memset(cur, 0, 20); cur[1] = 0x14; }
    bool changed = !lastValid[i] || memcmp(cur, lastRep[i], 20);
    if (!changed && now - lastSent[i] < 250) continue;     // resend now and then so a re-opened game gets the state
    if (usbd_edpt_busy(0, epIn[i])) continue;
    if (!usbd_edpt_claim(0, epIn[i])) continue;
    memcpy(r, cur, 20);
    if (usbd_edpt_xfer(0, epIn[i], r, 20)) { memcpy(lastRep[i], cur, 20); lastValid[i] = true; lastSent[i] = now; sent++; }
    else usbd_edpt_release(0, epIn[i]);
  }
}

void xinput_dump(char *buf, size_t n) {
  remap();
  size_t o = 0;
  o += snprintf(buf + o, n - o, "XInput mode %d (%s): usb mounted=%d, %d interface(s), reports sent %lu\n", mode, mode == 0 ? "1 pad" : mode == 1 ? "2 pads, wired-style" : "2 pads, receiver-style", tud_mounted(), nSlots, (unsigned long)sent);
  for (int k = 0; k < nSlots && o < n - 120; k++) {
    uint8_t r[20];
    if (ifToSlot[k] >= 0) build(r, gip_pad(ifToSlot[k])); else { memset(r, 0, 20); r[1] = 0x14; }
    o += snprintf(buf + o, n - o, "  interface %d <- controller slot %d: ", k, ifToSlot[k]);
    for (int i = 0; i < 14; i++) o += snprintf(buf + o, n - o, "%02x%s", r[i], i == 1 || i == 3 || i == 5 ? "|" : "");
    o += snprintf(buf + o, n - o, "  [btn %02x%02x LT %u RT %u LX %d LY %d RX %d RY %d]\n", r[3], r[2], r[4], r[5],
                  (int16_t)(r[6] | r[7] << 8), (int16_t)(r[8] | r[9] << 8), (int16_t)(r[10] | r[11] << 8), (int16_t)(r[12] | r[13] << 8));
  }
}

int hid_slots() { return nSlots; }
int hid_max_slots() { return nSlots; }   // XInput: the layout is chosen with the BOOT button, never auto-grown (that caused a restart loop)
void hid_request_slots(int n) {
  if (n < 1) n = 1;
  if (n > XI_MAX_IF) n = XI_MAX_IF;
  Preferences pr;
  pr.begin("xbhid", false);
  pr.putUChar("slots", n);
  pr.end();
}
bool hid_mounted() { return tud_mounted(); }
bool hid_suspended() { return tud_suspended(); }
bool hid_wake_host() { return tud_suspended() ? tud_remote_wakeup() : false; }
uint32_t hid_reports_sent() { return sent; }
uint32_t hid_rumble_msgs() { return rumbleMsgs; }
#endif
