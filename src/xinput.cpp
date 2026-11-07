// XInput (Xbox 360 wired controller) USB device for Windows, with an automatic number of controllers.
//
// Windows treats a device as a real Xbox controller (XInput: games show Xbox glyphs and use the Xbox mapping)
// when it exposes the Xbox 360 vendor interface: class 0xFF, subclass 0x5D, protocol 0x01, with one interrupt IN
// and one interrupt OUT endpoint. This implements that on top of TinyUSB's application class driver hook.
// XInput must be the ONLY USB function, so there is no USB serial here.
//
// One interface = one controller (XInput player slot). The number of interfaces follows the number of connected
// controllers: it grows shortly after a controller connects and shrinks only after the extra ones have been gone
// for a while. A change is applied by a soft USB disconnect/reconnect (the PC sees the device re-plugged, about
// one second), never by restarting the firmware. The descriptors are therefore produced on demand
// (tud_descriptor_*_cb are overridden), and the serial number changes with the layout so Windows treats each
// layout as a fresh device instead of reusing cached descriptors.
//
// Descriptor style A = class descriptor of a real wired pad (0x21); style B = the receiver-style 0x22 descriptor used
// by the multi-controller emulation in github.com/Berghopper/360-w-raw-gadget (MIT).
#include "config.h"
#ifdef XINPUT_MODE
#include "hid.h"
#include "settings.h"
#include "evlog.h"
#include "dlog.h"
#include <math.h>
#include "gip.h"
#include <Preferences.h>
#include "pairing.h"
#include "USB.h"
extern "C" {
#include "tusb.h"
#include "device/usbd_pvt.h"
#include "esp32-hal-tinyusb.h"
}

#define XI_MAX_IF 4
#define XI_IF_MAX_LEN (9 + 20 + 14)

static int nIf = 1;                 // interfaces currently exposed
static int style = 0;               // 0 = A, 1 = B
static uint8_t epIn[XI_MAX_IF], epOut[XI_MAX_IF];
static bool opened[XI_MAX_IF];
static uint8_t outBuf[XI_MAX_IF][32];
static uint8_t inBuf[XI_MAX_IF][20];
static uint8_t lastRep[XI_MAX_IF][20];
static bool lastValid[XI_MAX_IF];
static uint32_t lastSent[XI_MAX_IF];
static volatile uint32_t sent = 0, rumbleMsgs = 0;
static volatile int8_t ifToSlot[XI_MAX_IF];   // XInput interface k carries controller slot ifToSlot[k] (-1 = none)

// ---------------------------------------------------------------- descriptors (built on demand)
static uint8_t cfgDesc[9 + XI_MAX_IF * XI_IF_MAX_LEN];
static uint16_t cfgLen = 0;
static uint16_t serialStr[16];

static void buildConfig() {
  uint8_t *p = cfgDesc + 9;
  for (int i = 0; i < nIf; i++) {
    uint8_t in = 0x81 + i, out = 0x01 + i;     // endpoint number = interface + 1, IN and OUT share it like a real pad
    const uint8_t ifd[9] = {9, TUSB_DESC_INTERFACE, (uint8_t)i, 0, 2, 0xFF, 0x5D, 0x01, 0};
    memcpy(p, ifd, 9); p += 9;
    if (style == 1) {
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
  cfgLen = p - cfgDesc;
  // configuration header: bus powered, remote wakeup, 500 mA (like a real wired 360 pad)
  const uint8_t hdr[9] = {9, TUSB_DESC_CONFIGURATION, (uint8_t)(cfgLen & 0xff), (uint8_t)(cfgLen >> 8), (uint8_t)nIf, 1, 0, 0xA0, 250};
  memcpy(cfgDesc, hdr, 9);
  // serial number encodes the layout: a different (count, style) is a different device to Windows
  char sn[16];
  snprintf(sn, sizeof(sn), "XB36%d%c", nIf, 'A' + style);
  int n = strlen(sn);
  serialStr[0] = (TUSB_DESC_STRING << 8) | (2 * n + 2);
  for (int i = 0; i < n; i++) serialStr[1 + i] = sn[i];
}

extern "C" uint8_t const *tud_descriptor_configuration_cb(uint8_t) { return cfgDesc; }

extern "C" uint8_t const *tud_descriptor_device_cb(void) {
  static const tusb_desc_device_t dev = {
      .bLength = sizeof(tusb_desc_device_t), .bDescriptorType = TUSB_DESC_DEVICE, .bcdUSB = 0x0200,
      .bDeviceClass = 0xFF, .bDeviceSubClass = 0xFF, .bDeviceProtocol = 0xFF, .bMaxPacketSize0 = 64,
      .idVendor = 0x045E, .idProduct = 0x028E, .bcdDevice = 0x0114,
      .iManufacturer = 1, .iProduct = 2, .iSerialNumber = 3, .bNumConfigurations = 1};
  return (uint8_t const *)&dev;
}

extern "C" uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t) {
  static uint16_t buf[24];
  const char *s = nullptr;
  switch (index) {
    case 0: buf[0] = (TUSB_DESC_STRING << 8) | 4; buf[1] = 0x0409; return buf;
    case 1: s = "Microsoft"; break;
    case 2: s = "Controller"; break;
    case 3: return serialStr;
    default: return nullptr;
  }
  int n = strlen(s);
  buf[0] = (TUSB_DESC_STRING << 8) | (2 * n + 2);
  for (int i = 0; i < n; i++) buf[1 + i] = s[i];
  return buf;
}

// minimal interface so USB.begin() has something to start with; the real descriptors are the overrides above
static uint16_t dummyDescriptors(uint8_t *dst, uint8_t *itf) { (*itf)++; buildConfig(); memcpy(dst, cfgDesc + 9, cfgLen - 9); return cfgLen - 9; }

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
    // debug log reader: vendor request 0xB0, wValue = 240-byte chunk number, answers with that part of the log
    if (req->bmRequestType_bit.type == TUSB_REQ_TYPE_VENDOR && req->bmRequestType_bit.direction == TUSB_DIR_IN && req->bRequest == 0xB0) {
      static uint8_t chunk[240];
      size_t n = dlog_read((uint32_t)req->wValue * sizeof(chunk), chunk, sizeof(chunk));
      if (n > req->wLength) n = req->wLength;
      dlog_note_read();
      return tud_control_xfer(rhport, req, chunk, (uint16_t)n);
    }
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

// ---------------------------------------------------------------- controller -> interface mapping and reports
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

// radial stick deadzone: |v| below the threshold reads as centre, the rest is rescaled so full deflection stays full
static void deadzone(int16_t &x, int16_t &y, int pct) {
  if (pct <= 0) return;
  float fx = x, fy = y, m = sqrtf(fx * fx + fy * fy), t = 32768.0f * pct / 100.0f;
  if (m <= t) { x = 0; y = 0; return; }
  float k = (m - t) / (32768.0f - t) * 32768.0f / m;
  fx *= k; fy *= k;
  x = (int16_t)(fx > 32767 ? 32767 : fx < -32768 ? -32768 : fx);
  y = (int16_t)(fy > 32767 ? 32767 : fy < -32768 ? -32768 : fy);
}

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
  deadzone(v[0], v[1], cfg.deadzone * 5);
  deadzone(v[2], v[3], cfg.deadzone * 5);
  for (int i = 0; i < 4; i++) { r[6 + 2 * i] = v[i] & 0xff; r[7 + 2 * i] = (v[i] >> 8) & 0xff; }
}

// ---------------------------------------------------------------- automatic layout
enum { RE_IDLE, RE_DROPPED };
static int reState = RE_IDLE;
static uint32_t reAt = 0;
static int wantVal = 0;
static uint32_t wantSince = 0;

static int pendCount = 1, pendStyle = 0;
static volatile int styleRequest = -1;   // set from the UI task, consumed by hid_task() on the USB side

// Step 1: unplug. The descriptors are only rebuilt after the pause, so the host can never read half-new data.
static void applyLayout(int newCount, int newStyle) {
  evlog_add(newCount ? "USB re-plug: %d pad%s" : "USB unplugged: no pads", newCount, newCount == 1 ? "" : "s");
  pendCount = newCount;
  pendStyle = newStyle;
  for (int i = 0; i < XI_MAX_IF; i++) { opened[i] = false; lastValid[i] = false; }
  tud_disconnect();                      // the host sees the device unplugged ...
  reState = RE_DROPPED;
  reAt = millis();                       // ... and hid_task() plugs it back in after a short pause
}

void hid_begin() {
  // GPIO43/44 are the UART pins; on the T-Embed Plus they are the nRF24 CE/CSN lines, so no UART there.
  if (!HAS_SCREEN) Serial.begin(115200);
  Preferences pr;
  pr.begin("xbhid", true);
  style = pr.getUChar("xistyle", 0) & 1;
  pr.end();
  nIf = 1;
  buildConfig();
  tinyusb_enable_interface(USB_INTERFACE_CUSTOM, cfgLen - 9, dummyDescriptors);
  USB.VID(0x045E);
  USB.PID(0x028E);
  USB.usbClass(0xFF);
  USB.usbSubClass(0xFF);
  USB.usbProtocol(0xFF);
  USB.usbVersion(0x0200);
  USB.productName("Controller");
  USB.manufacturerName("Microsoft");
  USB.serialNumber("XB36");
  USB.usbAttributes(TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP);
  USB.begin();
}

int xinput_style() { return style; }
void xinput_set_style(int st) {
  st &= 1;
  Preferences pr;
  pr.begin("xbhid", false);
  pr.putUChar("xistyle", st);
  pr.end();
  if (st != style) styleRequest = st;    // applied by hid_task()
}

void hid_task() {
  uint32_t now = millis();
  if (reState == RE_DROPPED) {
    if (now - reAt > 900) {
      style = pendStyle;
      nIf = pendCount;
      if (nIf > 0) { buildConfig(); tud_connect(); }   // new descriptors while unplugged, then plug back in
      reState = RE_IDLE; wantSince = now;              // nIf == 0: stay unplugged, nothing is emulated
    }
    return;
  }
  if (styleRequest >= 0) { int st = styleRequest; styleRequest = -1; if (st != style) { applyLayout(nIf, st); return; } }
  if (nIf > 0 && !tud_mounted()) return;

  // The number of emulated Xbox pads follows the number of connected controllers exactly: 0, 1, 2, ...
  remap();
  int ready = 0;
  for (int i = 0; i < MAX_PADS; i++) if (gip_pad(i).stage == PAD_READY) ready++;
  int want = ready > XI_MAX_IF ? XI_MAX_IF : ready;
  if (want != wantVal) { wantVal = want; wantSince = now; }
  if (want > nIf && now - wantSince > 1200) { applyLayout(want, style); return; }          // a controller appeared
  // A controller went away. Wait a few seconds so a brief radio dropout doesn't unplug a pad, and never pull the
  // device while the computer is asleep (the Xbox button must still be able to wake it).
  // Going to zero pads also waits 30 s after boot and while a log reader is attached, so the log can always be read.
  bool keepForLog = want == 0 && (now < 30000 || (dlog_last_read_ms() && now - dlog_last_read_ms() < 30000));
  if (want < nIf && now - wantSince > 4000 && !tud_suspended() && !keepForLog) { applyLayout(want, style); return; }
  if (nIf == 0) return;

  for (int i = 0; i < nIf; i++) {
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
  o += snprintf(buf + o, n - o, "XInput style %c: usb mounted=%d, %d interface(s), reports sent %lu\n", 'A' + style, tud_mounted(), nIf, (unsigned long)sent);
  for (int k = 0; k < nIf && o < n - 120; k++) {
    uint8_t r[20];
    if (ifToSlot[k] >= 0) build(r, gip_pad(ifToSlot[k])); else { memset(r, 0, 20); r[1] = 0x14; }
    o += snprintf(buf + o, n - o, "  interface %d <- controller slot %d: ", k, ifToSlot[k]);
    for (int i = 0; i < 14; i++) o += snprintf(buf + o, n - o, "%02x%s", r[i], i == 1 || i == 3 || i == 5 ? "|" : "");
    o += snprintf(buf + o, n - o, "  [btn %02x%02x LT %u RT %u LX %d LY %d RX %d RY %d]\n", r[3], r[2], r[4], r[5],
                  (int16_t)(r[6] | r[7] << 8), (int16_t)(r[8] | r[9] << 8), (int16_t)(r[10] | r[11] << 8), (int16_t)(r[12] | r[13] << 8));
  }
}

int hid_slots() { return nIf; }
int hid_max_slots() { return XI_MAX_IF; }
void hid_request_slots(int) {}           // automatic: nothing to request
bool hid_mounted() { return tud_mounted(); }
bool hid_suspended() { return tud_suspended(); }
bool hid_wake_host() { return tud_suspended() ? tud_remote_wakeup() : false; }
uint32_t hid_reports_sent() { return sent; }
uint32_t hid_rumble_msgs() { return rumbleMsgs; }
#endif
