// Xbox Wireless "dongle" radio layer for the ESP32-S3.
// Mimics the MT76-based dongle as documented by the xone/xow drivers:
// a soft-AP interface with a spoofed MAC, a custom beacon carrying a
// Microsoft vendor IE, and raw 802.11 TX/RX.
#include "radio.h"
#include <WiFi.h>
#include "esp_wifi.h"
#include "esp_timer.h"
#include "freertos/queue.h"

// The Wi-Fi library normally rejects unusual management subtypes (the dongle
// protocol uses reserved subtype 7). Override its sanity check.
extern "C" int ieee80211_raw_frame_sanity_check(int32_t a, int32_t b, int32_t c) { return 0; }
extern "C" esp_err_t esp_wifi_internal_set_fix_rate(wifi_interface_t ifx, bool en, wifi_phy_rate_t rate);

static QueueHandle_t evq;
static RadioStats stats;
static volatile uint8_t curCh = 1;
static volatile bool pairFlag = false;
static bool scanning = true;
static uint16_t txSeq = 1;

// Registered controllers (written by main task, read by RX callback).
static uint8_t regMac[MAX_PADS][6];
static volatile bool regUsed[MAX_PADS];
static volatile uint32_t regLastRx[MAX_PADS];
static volatile uint16_t regLastSeq[MAX_PADS];
static volatile bool pendingTx[MAX_PADS];
static volatile uint16_t anySeq[MAX_PADS];      // last 802.11 sequence number of any frame from the controller
static volatile uint32_t lostFrames[MAX_PADS], rxFramesPad[MAX_PADS];

static int findSlot(const uint8_t *mac) {
  for (int i = 0; i < MAX_PADS; i++)
    if (regUsed[i] && !memcmp(regMac[i], mac, 6)) return i;
  return -1;
}

static void push(RadioEventKind k, const uint8_t *mac, int8_t rssi, const uint8_t *data, int len) {
  RadioEvent ev;
  ev.kind = k;
  memcpy(ev.mac, mac, 6);
  ev.channel = curCh;
  ev.rssi = rssi;
  if (len > (int)sizeof(ev.data)) len = sizeof(ev.data);
  if (len < 0) len = 0;
  ev.len = len;
  if (len) memcpy(ev.data, data, len);
  if (xQueueSend(evq, &ev, 0) != pdTRUE) stats.queueDrops++;
}

static void rxCb(void *buf, wifi_promiscuous_pkt_type_t) {
  const wifi_promiscuous_pkt_t *p = (wifi_promiscuous_pkt_t *)buf;
  const uint8_t *d = p->payload;
  int len = p->rx_ctrl.sig_len;
  stats.rxFrames++;
  if (len < 24 + 4) return;
  if (memcmp(d + 4, DONGLE_MAC, 6) != 0) return;  // only frames addressed to our BSSID
  stats.toMe++;
  uint8_t type = (d[0] >> 2) & 3, sub = (d[0] >> 4) & 15;
  const uint8_t *src = d + 10;
  bool retry = d[1] & 8;
  uint16_t seq = (d[22] | (d[23] << 8)) >> 4;

  int slot = findSlot(src);
  if (slot >= 0) {
    regLastRx[slot] = millis();
    // Sequence gaps are only meaningful within one stream: the controller numbers QoS-data frames
    // (which carry input) separately from its keepalive null frames.
    if (type == 2 && sub == 8) {
      if (!retry) {   // retries repeat a sequence number
        uint16_t exp = (anySeq[slot] + 1) & 0xfff;
        if (rxFramesPad[slot] && seq != exp) { uint16_t gap = (seq - exp) & 0xfff; if (gap < 64) lostFrames[slot] += gap; }
        anySeq[slot] = seq;
      }
      rxFramesPad[slot]++;   // QoS-data frames only
    }
    if (type == 2 && sub == 8 && retry && regLastSeq[slot] == seq) { stats.dupDropped++; return; }
    if (type == 2 && sub == 8) regLastSeq[slot] = seq;
  }

  if (slot >= 0 && pendingTx[slot] && type == 2 && sub == 12) { pendingTx[slot] = false; push(EV_NULL, src, p->rx_ctrl.rssi, nullptr, 0); return; }

  if (type == 0 && (sub == 0 || sub == 2)) {
    push(EV_ASSOC, src, p->rx_ctrl.rssi, nullptr, 0);
  } else if (type == 0 && sub == 7) {
    if (len >= 24 + 2 + 4 && d[24] == 0x70 && d[25] == 0x01 && !retry)
      push(EV_PAIRREQ, src, p->rx_ctrl.rssi, nullptr, 0);
  } else if (type == 2 && sub == 8) {
    // payload starts after the 26-byte QoS header, minus the 4-byte FCS
    push(EV_GIP, src, p->rx_ctrl.rssi, d + 26, len - 26 - 4);
  }
}

static bool rawTx(uint8_t *f, int n) {
  f[22] = (txSeq << 4) & 0xff;
  f[23] = (txSeq >> 4) & 0xff;
  txSeq = (txSeq + 1) & 0xfff;
  esp_err_t e = esp_wifi_80211_tx(WIFI_IF_AP, f, n, false);
  if (e != ESP_OK) { stats.txErr++; return false; }
  return true;
}

bool radio_send_qos(const uint8_t *mac, const uint8_t *gip, int len) {
  uint8_t f[26 + 128] = {0};
  if (len > 128) return false;
  f[0] = 0x88; f[1] = 0x02;  // QoS data, FromDS
  f[2] = 144;                // duration, as xone
  memcpy(f + 4, mac, 6);
  memcpy(f + 10, DONGLE_MAC, 6);
  memcpy(f + 16, DONGLE_MAC, 6);
  memcpy(f + 26, gip, len);
  return rawTx(f, 26 + len);
}

bool radio_send_mgmt(uint8_t fc0, const uint8_t *mac, const uint8_t *body, int len) {
  uint8_t f[24 + 64] = {0};
  if (len > 64) return false;
  f[0] = fc0; f[1] = 0x00;
  memcpy(f + 4, mac, 6);
  memcpy(f + 10, DONGLE_MAC, 6);
  memcpy(f + 16, DONGLE_MAC, 6);
  memcpy(f + 24, body, len);
  return rawTx(f, 24 + len);
}

static void sendBeacon() {
  uint8_t f[24 + 12 + 20] = {0};
  f[0] = 0x80;
  memset(f + 4, 0xff, 6);
  memcpy(f + 10, DONGLE_MAC, 6);
  memcpy(f + 16, DONGLE_MAC, 6);
  uint64_t ts = esp_timer_get_time();
  memcpy(f + 24, &ts, 8);
  f[32] = 100; f[33] = 0;        // beacon interval
  f[34] = 0x31; f[35] = 0xc6;    // capability info 0xc631 (as xone)
  const uint8_t ie[20] = {
      0x00, 0x00,                                // empty SSID
      0xdd, 0x10, 0x00, 0x50, 0xf2, 0x11,        // vendor IE: Microsoft OUI, type 0x11
      0x01, 0x10, (uint8_t)pairFlag, 0xa5, 0x30, 0x99,
      (uint8_t)curCh, 0x00, 0x00, 0x00, 0x00, 0x00};
  memcpy(f + 36, ie, sizeof(ie));
  rawTx(f, sizeof(f));
}

static void setChannel(uint8_t ch) {
  curCh = ch;
  esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
}

void radio_begin() {
  evq = xQueueCreate(32, sizeof(RadioEvent));
  WiFi.mode(WIFI_STA);
  esp_wifi_stop();
  esp_wifi_deinit();
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  cfg.ampdu_tx_enable = 0;  // needed to force a fixed OFDM rate
  esp_wifi_init(&cfg);
  esp_wifi_set_storage(WIFI_STORAGE_RAM);
  esp_wifi_set_mode(WIFI_MODE_AP);
  esp_wifi_set_mac(WIFI_IF_AP, (uint8_t *)DONGLE_MAC);
  wifi_config_t ap = {};
  strcpy((char *)ap.ap.ssid, "x");
  ap.ap.ssid_len = 1;
  ap.ap.ssid_hidden = 1;
  ap.ap.channel = 1;
  ap.ap.authmode = WIFI_AUTH_OPEN;
  ap.ap.max_connection = 1;
  ap.ap.beacon_interval = 100;
  esp_wifi_set_config(WIFI_IF_AP, &ap);
  esp_wifi_start();
  esp_wifi_set_max_tx_power(44);   // ~11 dBm: plenty for a controller at arm's length, easier on a USB-powered board
  esp_wifi_set_promiscuous_rx_cb(rxCb);
  wifi_promiscuous_filter_t flt = {.filter_mask = WIFI_PROMIS_FILTER_MASK_ALL};
  esp_wifi_set_promiscuous_filter(&flt);
  esp_wifi_set_promiscuous(true);
  esp_wifi_internal_set_fix_rate(WIFI_IF_AP, true, WIFI_PHY_RATE_6M);
  setChannel(SCAN_CHANNELS[0]);
}

void radio_tick() {
  static uint32_t lastBeacon = 0, lastHop = 0;
  static int idx = 0;
  uint32_t now = millis();
  bool anyClient = false;
  for (int i = 0; i < MAX_PADS; i++) if (regUsed[i]) anyClient = true;
  scanning = !anyClient;
  if (scanning && now - lastHop > SCAN_DWELL_MS) {
    lastHop = now;
    idx = (idx + 1) % (int)sizeof(SCAN_CHANNELS);
    setChannel(SCAN_CHANNELS[idx]);
  }
  if (now - lastBeacon >= 100) { lastBeacon = now; sendBeacon(); }
}

bool radio_poll(RadioEvent &ev) { return xQueueReceive(evq, &ev, 0) == pdTRUE; }

void radio_register(int slot, const uint8_t *mac) {
  if (slot < 0 || slot >= MAX_PADS) return;
  if (!mac) { regUsed[slot] = false; return; }
  regUsed[slot] = false;
  memcpy(regMac[slot], mac, 6);
  regLastRx[slot] = millis();
  regLastSeq[slot] = 0xffff;
  lostFrames[slot] = 0; rxFramesPad[slot] = 0;
  regUsed[slot] = true;
}

uint32_t radio_last_rx(int slot) { return regLastRx[slot]; }
void radio_set_pending_tx(int slot, bool on) { if (slot >= 0 && slot < MAX_PADS) pendingTx[slot] = on; }
uint32_t radio_lost_frames(int slot) { return lostFrames[slot]; }
uint32_t radio_rx_frames(int slot) { return rxFramesPad[slot]; }
void radio_set_pairing(bool on) { pairFlag = on; }
bool radio_pairing() { return pairFlag; }
uint8_t radio_channel() { return curCh; }
bool radio_scanning() { return scanning; }
const RadioStats &radio_stats() { return stats; }
