#include "updater.h"
#if HAS_SCREEN
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <SD.h>
#include <SPI.h>
#include <Preferences.h>
#include <esp_wps.h>
#include <esp_ota_ops.h>
#include "esp_sleep.h"
#include "esp_task_wdt.h"
#include "mbedtls/sha256.h"
#include "ui.h"
#include "dlog.h"

extern const uint8_t rootca_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");

#define BASE_URL "https://github.com/" UPDATE_REPO "/releases/latest/download/"

enum { CMD_NONE = 0, CMD_SCAN, CMD_CONNECT, CMD_WPS, CMD_CHECK, CMD_INSTALL };

static bool active = false;
static UpdView v;
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static volatile int pending = CMD_NONE;
static volatile bool cancelFlag = false;
static char cSsid[33], cPass[65];
static char mFile[64], mSha[72];
static uint32_t mSize = 0;
static char savedSsid[33], savedPass[65];

static void setState(UpdState st, const char *msg, const char *detail = "") {
  portENTER_CRITICAL(&mux);
  v.state = st;
  strlcpy(v.msg, msg, sizeof(v.msg));
  strlcpy(v.detail, detail, sizeof(v.detail));
  portEXIT_CRITICAL(&mux);
  dlog("UPD %d: %s %s", (int)st, msg, detail);
}

static void feed() { esp_task_wdt_reset(); delay(1); }

bool updater_boot_flag() {
  Preferences p;
  p.begin("xbupd", false);
  bool go = p.getBool("go", false);
  if (go) p.putBool("go", false);       // one shot: a crash can never trap the board in update mode
  p.end();
  return go;
}

void updater_request_and_restart() {
  Preferences p;
  p.begin("xbupd", false);
  p.putBool("go", true);
  p.end();
  delay(300);
  ESP.restart();
}

bool updater_active() { return active; }

void updater_exit() { ESP.restart(); }

void updater_view(UpdView &out) {
  portENTER_CRITICAL(&mux);
  out = v;
  portEXIT_CRITICAL(&mux);
}

void updater_begin() {
  active = true;
  esp_task_wdt_delete(NULL);   // network calls can block longer than the 5 s loop watchdog
  Preferences p;
  p.begin("xbupd", true);
  p.getString("ssid", savedSsid, sizeof(savedSsid));
  p.getString("pass", savedPass, sizeof(savedPass));
  p.end();
  memset(&v, 0, sizeof(v));
  strlcpy(v.saved, savedSsid, sizeof(v.saved));
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  setState(U_MENU, "Update mode");
}

void updater_cmd_menu() { cancelFlag = true; pending = CMD_NONE; setState(U_MENU, "Update mode"); }
void updater_cmd_scan() { pending = CMD_SCAN; cancelFlag = false; }
void updater_cmd_connect(const char *ssid, const char *pass) {
  strlcpy(cSsid, ssid, sizeof(cSsid)); strlcpy(cPass, pass, sizeof(cPass));
  pending = CMD_CONNECT; cancelFlag = false;
}
void updater_cmd_pass(const char *ssid) { setState(U_PASS, "Password", ssid); }
void updater_cmd_wps() { pending = CMD_WPS; cancelFlag = false; }
void updater_cmd_check() { pending = CMD_CHECK; cancelFlag = false; }
void updater_cmd_install() { pending = CMD_INSTALL; cancelFlag = false; }
void updater_cmd_cancel() { cancelFlag = true; }

// ---------------------------------------------------------------- helpers
static void saveCreds(const char *ssid, const char *pass) {
  strlcpy(savedSsid, ssid, sizeof(savedSsid)); strlcpy(savedPass, pass, sizeof(savedPass));
  Preferences p;
  p.begin("xbupd", false);
  p.putString("ssid", ssid);
  p.putString("pass", pass);
  p.end();
  portENTER_CRITICAL(&mux); strlcpy(v.saved, ssid, sizeof(v.saved)); portEXIT_CRITICAL(&mux);
}

static bool waitConnected(uint32_t ms) {
  uint32_t t0 = millis();
  while (millis() - t0 < ms && !cancelFlag) {
    wl_status_t st = WiFi.status();
    if (st == WL_CONNECTED) return true;
    feed();
    delay(100);
  }
  return WiFi.status() == WL_CONNECTED;
}

static bool connectTo(const char *ssid, const char *pass) {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  if (pass && pass[0]) WiFi.begin(ssid, pass); else WiFi.begin(ssid);
  return waitConnected(20000);
}

static bool syncTime() {
  configTime(0, 0, "pool.ntp.org", "time.google.com");     // certificate checks need the right date
  uint32_t t0 = millis();
  while (time(nullptr) < 1700000000 && millis() - t0 < 10000 && !cancelFlag) { feed(); delay(100); }
  return time(nullptr) >= 1700000000;
}

static bool ensureOnline() {
  if (WiFi.status() == WL_CONNECTED) return true;
  if (!savedSsid[0]) { setState(U_ERROR, "No Wi-Fi set up", "pick a network first"); return false; }
  setState(U_BUSY, "Connecting...", savedSsid);
  if (!connectTo(savedSsid, savedPass)) { if (!cancelFlag) setState(U_ERROR, "Could not connect", savedSsid); return false; }
  return true;
}

// ---------------------------------------------------------------- scan
static void doScan() {
  setState(U_BUSY, "Scanning for networks...");
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  int n = WiFi.scanNetworks(false, false, false, 300);
  portENTER_CRITICAL(&mux);
  v.nNets = 0;
  for (int i = 0; i < n && v.nNets < UPD_NETS; i++) {
    String s = WiFi.SSID(i);
    if (!s.length()) continue;
    bool dup = false;
    for (int k = 0; k < v.nNets; k++) if (!strcmp(v.ssid[k], s.c_str())) dup = true;
    if (dup) continue;
    strlcpy(v.ssid[v.nNets], s.c_str(), 33);
    v.rssi[v.nNets] = WiFi.RSSI(i);
    v.secure[v.nNets] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    v.nNets++;
  }
  portEXIT_CRITICAL(&mux);
  WiFi.scanDelete();
  setState(U_SCAN, "Choose your network");
}

bool updater_is_open(const char *ssid) {
  for (int k = 0; k < v.nNets; k++) if (!strcmp(v.ssid[k], ssid)) return !v.secure[k];
  return false;
}

// ---------------------------------------------------------------- connect / WPS
static void doConnect() {
  setState(U_BUSY, "Connecting...", cSsid);
  if (connectTo(cSsid, cPass)) {
    saveCreds(cSsid, cPass);
    String ip = WiFi.localIP().toString();
    setState(U_MENU, "Connected", ip.c_str());
  } else if (!cancelFlag) {
    setState(U_ERROR, "Could not connect", "wrong password?");
  }
}

static volatile int wpsResult = 0;   // 1 ok, 2 failed, 3 timeout
static void wpsEvent(arduino_event_id_t ev, arduino_event_info_t) {
  if (ev == ARDUINO_EVENT_WPS_ER_SUCCESS) wpsResult = 1;
  else if (ev == ARDUINO_EVENT_WPS_ER_FAILED) wpsResult = 2;
  else if (ev == ARDUINO_EVENT_WPS_ER_TIMEOUT) wpsResult = 3;
}

static void doWps() {
  wpsResult = 0;
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  WiFi.onEvent(wpsEvent);
  esp_wps_config_t cfg = WPS_CONFIG_INIT_DEFAULT(WPS_TYPE_PBC);
  esp_wifi_wps_enable(&cfg);
  esp_wifi_wps_start(0);
  setState(U_WPS, "Press the WPS button on your router");
  uint32_t t0 = millis();
  while (!wpsResult && !cancelFlag && millis() - t0 < 125000) {
    portENTER_CRITICAL(&mux); v.wpsLeftS = (125000 - (millis() - t0)) / 1000; portEXIT_CRITICAL(&mux);
    feed(); delay(100);
  }
  esp_wifi_wps_disable();
  if (wpsResult == 1) {
    delay(200);
    WiFi.begin();
    setState(U_BUSY, "Connecting...");
    if (waitConnected(20000)) {
      saveCreds(WiFi.SSID().c_str(), WiFi.psk().c_str());
      String ip = WiFi.localIP().toString();
      setState(U_MENU, "Connected via WPS", ip.c_str());
      return;
    }
    setState(U_ERROR, "WPS connected, but no link");
  } else if (!cancelFlag) {
    setState(U_ERROR, wpsResult == 2 ? "WPS failed" : "WPS timed out", "try the password instead");
  }
}

// ---------------------------------------------------------------- manifest / download
static bool jsonStr(const String &body, const char *key, char *out, size_t n) {
  String k = String("\"") + key + "\"";
  int i = body.indexOf(k);
  if (i < 0) return false;
  i = body.indexOf(':', i);
  if (i < 0) return false;
  int a = body.indexOf('"', i);
  int b = a < 0 ? -1 : body.indexOf('"', a + 1);
  if (a < 0 || b < 0) return false;
  strlcpy(out, body.substring(a + 1, b).c_str(), n);
  return true;
}
static bool jsonNum(const String &body, const char *key, uint32_t &out) {
  String k = String("\"") + key + "\"";
  int i = body.indexOf(k);
  if (i < 0) return false;
  i = body.indexOf(':', i);
  if (i < 0) return false;
  out = (uint32_t)body.substring(i + 1).toInt();
  return out > 0;
}

static bool verNewer(const char *a, const char *b) {   // is a newer than b ("1.10" > "1.9")
  int x[3] = {0, 0, 0}, y[3] = {0, 0, 0};
  sscanf(a, "%d.%d.%d", &x[0], &x[1], &x[2]);
  sscanf(b, "%d.%d.%d", &y[0], &y[1], &y[2]);
  for (int i = 0; i < 3; i++) { if (x[i] != y[i]) return x[i] > y[i]; }
  return false;
}

static void doCheck() {
  if (!ensureOnline()) return;
  setState(U_BUSY, "Checking for updates...");
  syncTime();
  WiFiClientSecure client;
  client.setCACertBundle(rootca_crt_bundle_start);
  HTTPClient http;
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.setTimeout(12000);
  if (!http.begin(client, BASE_URL "latest.json")) { setState(U_ERROR, "Could not start the request"); return; }
  int code = http.GET();
  if (code != 200) {
    char d[40]; snprintf(d, sizeof(d), code < 0 ? "network error %d" : "server answered %d", code);
    http.end();
    setState(U_ERROR, "Could not reach GitHub", d);
    return;
  }
  String body = http.getString();
  http.end();
  char ver[12] = {0};
  if (!jsonStr(body, "version", ver, sizeof(ver)) || !jsonStr(body, "file", mFile, sizeof(mFile)) ||
      !jsonStr(body, "sha256", mSha, sizeof(mSha)) || !jsonNum(body, "size", mSize)) {
    setState(U_ERROR, "Update info is unreadable");
    return;
  }
  if (!verNewer(ver, FW_VERSION)) { setState(U_UPTODATE, "You are up to date", FW_VERSION); return; }
  portENTER_CRITICAL(&mux); strlcpy(v.newVer, ver, sizeof(v.newVer)); v.size = mSize; portEXIT_CRITICAL(&mux);
  char d[40]; snprintf(d, sizeof(d), "%s  ->  %s", FW_VERSION, ver);
  setState(U_READY, "Update available", d);
}

static void hex(const uint8_t *in, char *out) { for (int i = 0; i < 32; i++) sprintf(out + i * 2, "%02x", in[i]); out[64] = 0; }

static void doInstall() {
  if (!ensureOnline()) return;
  setState(U_DOWNLOAD, "Preparing the SD card...");
  ui_spi_lock();
  bool sdOk = SD.begin(PIN_SD_CS, SPI, 16000000);
  if (sdOk && !SD.exists("/downloads")) SD.mkdir("/downloads");
  ui_spi_unlock();
  if (!sdOk) { setState(U_ERROR, "No SD card found", "insert the card and retry"); return; }

  char finalPath[96], partPath[100];
  snprintf(finalPath, sizeof(finalPath), "/downloads/XboxBridge.XInput.%s.bin", v.newVer);
  snprintf(partPath, sizeof(partPath), "%s.part", finalPath);

  WiFiClientSecure client;
  client.setCACertBundle(rootca_crt_bundle_start);
  HTTPClient http;
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.setTimeout(15000);
  char url[200]; snprintf(url, sizeof(url), BASE_URL "%s", mFile);
  setState(U_DOWNLOAD, "Downloading...", v.newVer);
  if (!http.begin(client, url) || http.GET() != 200) { http.end(); setState(U_ERROR, "Download failed", "could not start"); ui_spi_lock(); SD.end(); ui_spi_unlock(); return; }

  ui_spi_lock(); SD.remove(partPath); File f = SD.open(partPath, FILE_WRITE); ui_spi_unlock();
  if (!f) { http.end(); setState(U_ERROR, "Cannot write to the SD card"); return; }
  mbedtls_sha256_context sha; mbedtls_sha256_init(&sha); mbedtls_sha256_starts_ret(&sha, 0);
  static uint8_t buf[2048];
  WiFiClient *st = http.getStreamPtr();
  uint32_t got = 0, lastData = millis();
  bool ok = true;
  while (got < mSize && !cancelFlag) {
    int avail = st->available();
    if (avail <= 0) {
      if (!st->connected() && !st->available()) { ok = false; break; }
      if (millis() - lastData > 20000) { ok = false; break; }
      feed(); delay(2); continue;
    }
    int n = st->readBytes(buf, avail > (int)sizeof(buf) ? sizeof(buf) : avail);
    if (n <= 0) continue;
    if (got + n > mSize) n = mSize - got;
    mbedtls_sha256_update_ret(&sha, buf, n);
    ui_spi_lock(); size_t w = f.write(buf, n); ui_spi_unlock();
    if (w != (size_t)n) { ok = false; break; }
    got += n; lastData = millis();
    portENTER_CRITICAL(&mux); v.progress = (uint8_t)((uint64_t)got * 100 / mSize); portEXIT_CRITICAL(&mux);
    feed();
  }
  http.end();
  ui_spi_lock(); f.close(); ui_spi_unlock();
  uint8_t dig[32]; mbedtls_sha256_finish_ret(&sha, dig); mbedtls_sha256_free(&sha);
  if (cancelFlag) { ui_spi_lock(); SD.remove(partPath); SD.end(); ui_spi_unlock(); setState(U_MENU, "Cancelled"); return; }
  char got_hex[72]; hex(dig, got_hex);
  if (!ok || got != mSize) { ui_spi_lock(); SD.remove(partPath); SD.end(); ui_spi_unlock(); setState(U_ERROR, "Download interrupted", "try again"); return; }
  if (strcasecmp(got_hex, mSha) != 0) {
    dlog("UPD sha mismatch got=%s want=%s", got_hex, mSha);
    ui_spi_lock(); SD.remove(partPath); SD.end(); ui_spi_unlock();
    setState(U_ERROR, "Checksum does not match", "file discarded");
    return;
  }
  // verified: move into place and remove older Xbox Bridge builds
  ui_spi_lock();
  SD.remove(finalPath);
  SD.rename(partPath, finalPath);
  String old[8]; int no = 0;
  File dir = SD.open("/downloads");
  if (dir) { for (File e = dir.openNextFile(); e && no < 8; e = dir.openNextFile()) { String nm = e.name(); if (nm.startsWith("XboxBridge.XInput.") && nm.endsWith(".bin") && !String(finalPath).endsWith(nm)) old[no++] = nm; e.close(); } dir.close(); }
  for (int i = 0; i < no; i++) SD.remove(String("/downloads/") + old[i]);
  SD.end();
  ui_spi_unlock();
  setState(U_DONE, "Saved to the SD card", "restarting into Launcher");
  for (int s = 6; s > 0 && !cancelFlag; s--) { portENTER_CRITICAL(&mux); v.doneLeftS = s; portEXIT_CRITICAL(&mux); for (int k = 0; k < 10; k++) { feed(); delay(100); } }
  // A software restart goes straight back into this app. Launcher shows its screen after a power-on or a deep-sleep wake,
  // so go to deep sleep and let a timer wake the board a moment later.
  dlog("UPD: deep sleep for 1.5 s so Launcher starts");
  esp_sleep_enable_timer_wakeup(1500000);
  esp_deep_sleep_start();
}

void updater_tick() {
  int c = pending;
  if (c == CMD_NONE) { delay(5); return; }
  pending = CMD_NONE;
  switch (c) {
    case CMD_SCAN: doScan(); break;
    case CMD_CONNECT: doConnect(); break;
    case CMD_WPS: doWps(); break;
    case CMD_CHECK: doCheck(); break;
    case CMD_INSTALL: doInstall(); break;
  }
  if (cancelFlag && v.state == U_BUSY) setState(U_MENU, "Update mode");
}
#endif
