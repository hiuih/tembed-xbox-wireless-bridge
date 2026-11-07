#include "ble.h"
#ifdef BLE_ENABLED
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include "hid.h"
#include "gip.h"
#include "evlog.h"
#include "dlog.h"
#include "settings.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"

static NimBLEServer *server = nullptr;
static NimBLEAdvertising *adv = nullptr;
static NimBLECharacteristic *inputs[BLE_PADS];
static volatile int state = BLE_OFF;
static bool inited = false;
// If the previous run crashed while starting Bluetooth, this is still set: Bluetooth is then switched off for good
// (otherwise a crash here would be a boot loop).
static RTC_NOINIT_ATTR uint32_t bleTrying;
#define BLE_TRYING 0xB1E00001u
#define BLE_MIN_HEAP 56000

// One Gamepad collection per pad (report id 1..4): 16 buttons, hat, 4 sticks (16 bit), 2 triggers (10 bit in 16).
// Report payload (no id): buttons u16, hat u8 (low nibble, 0 = none, 1..8 clockwise from up), LX LY RX RY i16, RT LT u16 = 15 bytes.
static const uint8_t kColl[] = {
  0x05, 0x01, 0x09, 0x05, 0xA1, 0x01, 0x85, 0x00,
  0x05, 0x09, 0x19, 0x01, 0x29, 0x10, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x10, 0x81, 0x02,
  0x05, 0x01, 0x09, 0x39, 0x15, 0x01, 0x25, 0x08, 0x75, 0x04, 0x95, 0x01, 0x81, 0x42,
  0x75, 0x04, 0x95, 0x01, 0x81, 0x03,
  0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x33, 0x09, 0x34, 0x16, 0x00, 0x80, 0x26, 0xFF, 0x7F, 0x75, 0x10, 0x95, 0x04, 0x81, 0x02,
  0x05, 0x02, 0x09, 0xC4, 0x09, 0xC5, 0x15, 0x00, 0x26, 0xFF, 0x03, 0x75, 0x10, 0x95, 0x02, 0x81, 0x02,
  0xC0};
static uint8_t reportMap[sizeof(kColl) * BLE_PADS];

static void buildMap() {
  for (int i = 0; i < BLE_PADS; i++) {
    uint8_t *d = reportMap + i * sizeof(kColl);
    memcpy(d, kColl, sizeof(kColl));
    d[7] = i + 1;
  }
}

class SrvCb : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *s, ble_gap_conn_desc *desc) override {
    state = BLE_CONNECTED;
    s->updateConnParams(desc->conn_handle, 6, 9, 0, 300);   // 7.5-11 ms: low latency
    evlog_add("+Bluetooth connected");
  }
  void onDisconnect(NimBLEServer *) override {
    state = BLE_ADVERTISING;
    evlog_add("-Bluetooth disconnected");
    if (adv) adv->start();
  }
};
static SrvCb srvCb;

void ble_begin(bool on) {
  if (!on) return;
  if (bleTrying == BLE_TRYING) {
    bleTrying = 0;
    cfg.bleOn = false; settings_save();
    dlog("BLE: the last start crashed -> Bluetooth switched off");
    evlog_add("-Bluetooth crashed: now off");
    return;
  }
  size_t freeB = heap_caps_get_free_size(MALLOC_CAP_INTERNAL), bigB = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
  dlog("BLE: heap before init free=%u largest=%u (need %u)", (unsigned)freeB, (unsigned)bigB, BLE_MIN_HEAP);
  if (freeB < BLE_MIN_HEAP) {
    dlog("BLE: not enough memory, not starting");
    evlog_add("!Bluetooth: not enough memory");
    return;
  }
  bleTrying = BLE_TRYING;
  buildMap();
  dlog("BLE: step 1 NimBLEDevice::init, free=%u", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  NimBLEDevice::init("Xbox Bridge");
  dlog("BLE: step 2 init returned, free=%u initialized=%d", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (int)NimBLEDevice::getInitialized());
  if (!NimBLEDevice::getInitialized()) {
    bleTrying = 0;
    dlog("BLE: init failed");
    evlog_add("-Bluetooth failed to start");
    return;
  }
  NimBLEDevice::setSecurityAuth(true, false, true);          // bond, no MITM, secure connections
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);  // "just works" pairing
  NimBLEDevice::setPower(ESP_PWR_LVL_P3);
  dlog("BLE: step 3 createServer");
  server = NimBLEDevice::createServer();
  server->setCallbacks(&srvCb);
  dlog("BLE: step 4 HID device, free=%u", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  NimBLEHIDDevice *hid = new NimBLEHIDDevice(server);
  hid->manufacturer()->setValue("Xbox Bridge");
  hid->pnp(0x02, 0x303A, 0x4B01, 0x0100);
  hid->hidInfo(0x00, 0x01);
  for (int i = 0; i < BLE_PADS; i++) inputs[i] = hid->inputReport(i + 1);
  hid->reportMap(reportMap, sizeof(reportMap));
  dlog("BLE: step 5 startServices, free=%u", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  hid->startServices();
  hid->setBatteryLevel(100);
  adv = NimBLEDevice::getAdvertising();
  adv->setAppearance(0x03C4);                                // HID gamepad
  adv->addServiceUUID(hid->hidService()->getUUID());
  adv->setScanResponse(true);
  dlog("BLE: step 6 advertising, free=%u", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  adv->start();
  inited = true;
  state = BLE_ADVERTISING;
  // bleTrying stays set until the board has run for a while (see ble_tick): a crash shortly after the start disables Bluetooth
  dlog("BLE up: advertising as 'Xbox Bridge', %d pad collections, heap now free=%u", BLE_PADS, (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  evlog_add("Bluetooth advertising");
}

static void pack(uint8_t *r, const Pad *p) {
  memset(r, 0, 15);
  if (!p || p->stage != PAD_READY) return;
  const PadInput &in = p->in;
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
  bool u = b & GIP_BTN_DUP, d = b & GIP_BTN_DDOWN, l = b & GIP_BTN_DLEFT, rt = b & GIP_BTN_DRIGHT;
  uint8_t hat = 0;
  if (u && rt) hat = 2; else if (rt && d) hat = 4; else if (d && l) hat = 6; else if (l && u) hat = 8;
  else if (u) hat = 1; else if (rt) hat = 3; else if (d) hat = 5; else if (l) hat = 7;
  r[0] = o & 0xff; r[1] = o >> 8; r[2] = hat;
  int16_t ax[4] = {in.lx, in.ly, in.rx, in.ry};      // Y: PadInput already uses HID's "down is positive"
  for (int i = 0; i < 4; i++) { r[3 + 2 * i] = ax[i] & 0xff; r[4 + 2 * i] = (ax[i] >> 8) & 0xff; }
  uint16_t t1 = in.rt > 1023 ? 1023 : in.rt, t2 = in.lt > 1023 ? 1023 : in.lt;   // accelerator = RT, brake = LT
  r[11] = t1 & 0xff; r[12] = t1 >> 8; r[13] = t2 & 0xff; r[14] = t2 >> 8;
}

void ble_tick() {
  if (inited && bleTrying && millis() > 20000) { bleTrying = 0; dlog("BLE: survived 20 s, start confirmed"); }
  if (!inited || state != BLE_CONNECTED) return;
  static uint32_t lastTx = 0, lastSent[BLE_PADS];
  static uint8_t prev[BLE_PADS][15];
  static bool have[BLE_PADS];
  uint32_t now = millis();
  if (now - lastTx < 8) return;
  lastTx = now;
  for (int i = 0; i < BLE_PADS; i++) {
    int slot = hid_pad_slot(i);
    uint8_t cur[15];
    pack(cur, slot >= 0 ? &gip_pad(slot) : nullptr);
    if (have[i] && !memcmp(cur, prev[i], 15) && now - lastSent[i] < 1000) continue;
    inputs[i]->setValue(cur, 15);
    inputs[i]->notify();
    memcpy(prev[i], cur, 15); have[i] = true; lastSent[i] = now;
  }
}

int ble_state() { return state; }

bool ble_forget_hosts() {
  if (!inited) return false;
  NimBLEDevice::deleteAllBonds();
  dlog("BLE: all bonds deleted");
  evlog_add("!Bluetooth hosts forgotten");
  return true;
}
#endif
