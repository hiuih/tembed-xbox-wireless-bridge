// Xbox Wireless "dongle" radio layer for the ESP32-S3.
// Mimics the MT76-based dongle as documented by the xone/xow drivers:
// a soft-AP interface with a spoofed MAC, a custom beacon carrying a
// Microsoft vendor IE, and raw 802.11 TX/RX.
#include "radio.h"
#include "ble.h"
#include <WiFi.h>
#include "esp_wifi.h"
#include "esp_timer.h"
#include "freertos/queue.h"

// The Wi-Fi library normally rejects unusual management subtypes (the dongle
// protocol uses reserved subtype 7). Override its sanity check.
extern "C" int ieee80211_raw_frame_sanity_check(int32_t a, int32_t b, int32_t c) { return 0; }
extern "C" esp_err_t esp_wifi_internal_set_fix_rate(wifi_interface_t ifx, bool en, wifi_phy_rate_t rate);

static QueueHandle_t evq;
static volatile uint32_t qMax = 0;
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
// per-type sequence tracking: [0]=QoS data, [1]=QoS null, [2]=both combined
static volatile uint16_t sq[MAX_PADS][3];
static volatile uint32_t sqFrames[MAX_PADS][3], sqLost[MAX_PADS][3];
static int txPower = 44, filterMode = 0, forcedCh = 0;
#ifdef RADIO_DEBUG
// census of every frame transmitted by registered controller slot 0, BEFORE the 'addressed to us' filter:
// [type][subtype][RA class 0=us 1=broadcast/multicast 2=someone else]  and a combined all-frames sequence check
static volatile uint32_t census[4][16][3];
static volatile uint16_t anySq = 0xffff;
static volatile uint32_t anyFrames = 0, anyLost = 0, anyRetry = 0;
struct VTrace { uint32_t us; uint16_t seq; uint8_t retry, rate, mode, b0, b1, b2, bw; uint16_t len; };
static VTrace vtr[48]; static volatile uint8_t vtrHead = 0;
static volatile uint32_t ackPol[2][4];      // [data=0/null=1][QoS ack policy]
static volatile uint32_t rateCnt[2][32];    // [data/null][PHY rate code]
static volatile uint32_t sigMode[2][3];     // 0 legacy, 1 HT, 2 VHT
static volatile uint32_t cwbCnt[2][2];      // 20 MHz / 40 MHz
static volatile uint32_t shortGi[2][2];
#endif


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
  else { uint32_t w = uxQueueMessagesWaiting(evq); if (w > qMax) qMax = w; }
}

static void rxCb(void *buf, wifi_promiscuous_pkt_type_t) {
  const wifi_promiscuous_pkt_t *p = (wifi_promiscuous_pkt_t *)buf;
  const uint8_t *d = p->payload;
  int len = p->rx_ctrl.sig_len;
  stats.rxFrames++;
  if (len < 24 + 4) return;
#ifdef RADIO_DEBUG
  {   // census (slot 0 only)
    const uint8_t *tx = d + 10;
    if (regUsed[0] && !memcmp(tx, regMac[0], 6)) {
      uint8_t ty = (d[0] >> 2) & 3, su = (d[0] >> 4) & 15;
      int ra = !memcmp(d + 4, DONGLE_MAC, 6) ? 0 : (d[4] & 1) ? 1 : 2;
      census[ty][su][ra]++;
      if (ty == 0 && su == 7 && len > 27) {
        VTrace &t = vtr[vtrHead % 48]; vtrHead++;
        t.us = (uint32_t)esp_timer_get_time(); t.seq = (d[22] | (d[23] << 8)) >> 4; t.retry = (d[1] >> 3) & 1;
        t.rate = p->rx_ctrl.rate & 31; t.mode = p->rx_ctrl.sig_mode; t.bw = p->rx_ctrl.cwb; t.len = len; t.b0 = d[24]; t.b1 = d[25]; t.b2 = d[26];
      }
      if (ty == 2 && (su == 8 || su == 12)) {
        int k = su == 12;
        ackPol[k][(d[24] >> 5) & 3]++;
        rateCnt[k][p->rx_ctrl.rate & 31]++;
        sigMode[k][p->rx_ctrl.sig_mode > 2 ? 2 : p->rx_ctrl.sig_mode]++;
        cwbCnt[k][p->rx_ctrl.cwb ? 1 : 0]++;
        shortGi[k][p->rx_ctrl.sgi ? 1 : 0]++;
      }
      if (d[1] & 8) anyRetry++;
      else {
        uint16_t sq16 = (d[22] | (d[23] << 8)) >> 4;
        if (anySq != 0xffff) { uint16_t e = (anySq + 1) & 0xfff; if (sq16 != e) { uint16_t g = (sq16 - e) & 0xfff; if (g < 64) anyLost += g; } }
        anySq = sq16; anyFrames++;
      }
    }
  }
#endif
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
    if (!retry && type == 2 && (sub == 8 || sub == 12)) {
      for (int k = 0; k < 3; k++) {
        bool use = (k == 0 && sub == 8) || (k == 1 && sub == 12) || k == 2;
        if (!use) continue;
        uint16_t exp = (sq[slot][k] + 1) & 0xfff;
        if (sqFrames[slot][k] && seq != exp) { uint16_t gap = (seq - exp) & 0xfff; if (gap < 64) sqLost[slot][k] += gap; }
        sq[slot][k] = seq; sqFrames[slot][k]++;
      }
    }
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
  evq = xQueueCreate(64, sizeof(RadioEvent));
  WiFi.mode(WIFI_STA);
  esp_wifi_stop();
  esp_wifi_deinit();
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  cfg.ampdu_tx_enable = 0;  // needed to force a fixed OFDM rate
  cfg.static_rx_buf_num = 24;    // promiscuous mode on a busy band: more RX buffers so frames are not dropped after being ACKed
  cfg.dynamic_rx_buf_num = 64;
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
  // With Bluetooth running, 'no power save' aborts inside the Wi-Fi/Bluetooth coexistence code (pm_set_sleep_type); an AP
  // never power-saves anyway, so the call is simply skipped then.
  if (ble_state() == BLE_OFF) esp_wifi_set_ps(WIFI_PS_NONE);
  esp_wifi_set_max_tx_power(44);   // ~11 dBm: plenty for a controller at arm's length, easier on a USB-powered board
  esp_wifi_set_promiscuous_rx_cb(rxCb);
  wifi_promiscuous_filter_t flt = {.filter_mask = WIFI_PROMIS_FILTER_MASK_ALL};
  esp_wifi_set_promiscuous_filter(&flt);
  esp_wifi_set_promiscuous(true);
  radio_set_filter(1);                      // management + data only: far fewer callbacks on a busy band
  esp_wifi_internal_set_fix_rate(WIFI_IF_AP, true, WIFI_PHY_RATE_6M);
  setChannel(SCAN_CHANNELS[0]);
}

void radio_tick() {
  static uint32_t lastBeacon = 0, lastHop = 0;
  static int idx = 0;
  uint32_t now = millis();
  bool anyClient = false;
  for (int i = 0; i < MAX_PADS; i++) if (regUsed[i]) anyClient = true;
  scanning = !anyClient && !forcedCh;
  if (forcedCh && curCh != forcedCh) setChannel(forcedCh);
  if (scanning && now - lastHop > SCAN_DWELL_MS) {
    lastHop = now;
    idx = (idx + 1) % (int)sizeof(SCAN_CHANNELS);
    setChannel(SCAN_CHANNELS[idx]);
  }
  if (now - lastBeacon >= 100) { lastBeacon = now; sendBeacon(); }
}

uint32_t radio_queue_max() { uint32_t m = qMax; qMax = 0; return m; }
bool radio_poll(RadioEvent &ev) { return xQueueReceive(evq, &ev, 0) == pdTRUE; }

void radio_register(int slot, const uint8_t *mac) {
  if (slot < 0 || slot >= MAX_PADS) return;
  if (!mac) { regUsed[slot] = false; return; }
  regUsed[slot] = false;
  memcpy(regMac[slot], mac, 6);
  regLastRx[slot] = millis();
  regLastSeq[slot] = 0xffff;
  lostFrames[slot] = 0; rxFramesPad[slot] = 0;
  for (int k = 0; k < 3; k++) { sqFrames[slot][k] = 0; sqLost[slot][k] = 0; }
  regUsed[slot] = true;
}

uint32_t radio_last_rx(int slot) { return regLastRx[slot]; }
void radio_set_pending_tx(int slot, bool on) { if (slot >= 0 && slot < MAX_PADS) pendingTx[slot] = on; }
void radio_seq_report(int slot, char *buf, size_t n) {
  snprintf(buf, n, "seq[data %lu/%lu null %lu/%lu all %lu/%lu](lost/seen)", (unsigned long)sqLost[slot][0], (unsigned long)sqFrames[slot][0],
           (unsigned long)sqLost[slot][1], (unsigned long)sqFrames[slot][1], (unsigned long)sqLost[slot][2], (unsigned long)sqFrames[slot][2]);
}
#ifdef RADIO_DEBUG
void radio_census(int slot, char *buf, size_t n) {
  (void)slot;
  size_t o = snprintf(buf, n, "all-frames seq: lost %lu / seen %lu (retries %lu)\n", (unsigned long)anyLost, (unsigned long)anyFrames, (unsigned long)anyRetry);
  for (int k = 0; k < 2; k++) {
    const char *nm = k ? "null" : "data";
    o += snprintf(buf + o, n - o, "    %s: ackpolicy[normal %lu none %lu noexpl %lu blk %lu] mode[legacy %lu HT %lu] bw[20 %lu 40 %lu] sgi[%lu/%lu]\n", nm,
                  (unsigned long)ackPol[k][0], (unsigned long)ackPol[k][1], (unsigned long)ackPol[k][2], (unsigned long)ackPol[k][3],
                  (unsigned long)sigMode[k][0], (unsigned long)sigMode[k][1], (unsigned long)cwbCnt[k][0], (unsigned long)cwbCnt[k][1],
                  (unsigned long)shortGi[k][0], (unsigned long)shortGi[k][1]);
    o += snprintf(buf + o, n - o, "    %s rates:", nm);
    for (int r = 0; r < 32; r++) if (rateCnt[k][r]) o += snprintf(buf + o, n - o, " code%d=%lu", r, (unsigned long)rateCnt[k][r]);
    o += snprintf(buf + o, n - o, "\n");
  }
  static const char *ras[3] = {"->us", "->bcast", "->other"};
  for (int t = 0; t < 4; t++) for (int u = 0; u < 16; u++) for (int r = 0; r < 3; r++)
    if (census[t][u][r] && o < n - 40) o += snprintf(buf + o, n - o, "    type%d/sub%-2d %-7s %lu\n", t, u, ras[r], (unsigned long)census[t][u][r]);
}
void radio_force_channel(int ch) { forcedCh = (ch >= 1 && ch <= 11) ? ch : 0; if (forcedCh) setChannel(forcedCh); }
int radio_forced_channel() { return forcedCh; }
static bool tweaked = false;
void radio_tweak(bool on) {
  tweaked = on;
  wifi_config_t ap = {};
  esp_wifi_get_config(WIFI_IF_AP, &ap);
  ap.ap.beacon_interval = on ? 5000 : 100;     // the driver's own (IE-less) hidden-SSID beacons: make them rare
  esp_wifi_set_config(WIFI_IF_AP, &ap);
  esp_wifi_set_protocol(WIFI_IF_AP, on ? WIFI_PROTOCOL_11G : (WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N));
}
bool radio_tweaked() { return tweaked; }
void radio_trace_vendor(char *buf, size_t n) {
  size_t o = 0; uint8_t cnt = vtrHead < 48 ? vtrHead : 48; uint32_t prev = 0;
  for (uint8_t i = 0; i < cnt && o < n - 90; i++) {
    VTrace &t = vtr[(vtrHead - cnt + i) % 48];
    o += snprintf(buf + o, n - o, "    +%7lu us  seq %4u %s rate%u mode%u bw%u len%u  %02x %02x %02x\n", (unsigned long)(prev ? t.us - prev : 0), t.seq, t.retry ? "RETRY" : "first", t.rate, t.mode, t.bw, t.len, t.b0, t.b1, t.b2);
    prev = t.us;
  }
}
#else
void radio_census(int, char *buf, size_t) { buf[0] = 0; }
void radio_trace_vendor(char *buf, size_t) { buf[0] = 0; }
#endif
void radio_set_tx_power(int q) { if (q < 8) q = 8; if (q > 84) q = 84; txPower = q; esp_wifi_set_max_tx_power(q); }
int radio_tx_power() { return txPower; }
void radio_set_filter(int mode) {
  filterMode = mode;
  wifi_promiscuous_filter_t f = {.filter_mask = mode == 1 ? (WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA) : WIFI_PROMIS_FILTER_MASK_ALL};
  esp_wifi_set_promiscuous_filter(&f);
}
int radio_filter() { return filterMode; }
uint32_t radio_lost_frames(int slot) { return lostFrames[slot]; }
uint32_t radio_rx_frames(int slot) { return rxFramesPad[slot]; }
void radio_set_pairing(bool on) { pairFlag = on; }
bool radio_pairing() { return pairFlag; }
uint8_t radio_channel() { return curCh; }
bool radio_scanning() { return scanning; }
const RadioStats &radio_stats() { return stats; }
