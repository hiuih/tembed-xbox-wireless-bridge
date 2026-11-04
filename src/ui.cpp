#include "ui.h"
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include "gip.h"
#include "hid.h"
#include "pairing.h"
#include "bootguard.h"
#include "leds.h"
#include "esp_task_wdt.h"

static Adafruit_ST7789 tft(&SPI, PIN_LCD_CS, PIN_LCD_DC, -1);
static GFXcanvas16 *cv;

#define C_BG 0x0000
#define C_FG 0xFFFF
#define C_DIM 0x7BEF
#define C_ACC 0x07FF
#define C_GRN 0x07E0
#define C_RED 0xF800
#define C_YEL 0xFFE0
#define C_BLU 0x021F
#define C_SEL 0x18E3

// ---------------------------------------------------------------- input
static volatile int encDelta = 0;
static void IRAM_ATTR encIsr() { encDelta += digitalRead(PIN_ENC_B) ? -1 : 1; }

enum Btn { B_NONE, B_CLICK, B_LONG };
static Btn pollButton() {
  static uint32_t downAt = 0, lastEdge = 0;
  static bool down = false, longDone = false;
  bool now = digitalRead(PIN_ENC_KEY) == LOW;
  uint32_t t = millis();
  if (now != down && t - lastEdge > 25) {
    lastEdge = t; down = now;
    if (down) { downAt = t; longDone = false; }
    else if (!longDone && t - downAt < 1000) return B_CLICK;
  }
  if (down && !longDone && t - downAt >= 1000) { longDone = true; return B_LONG; }
  return B_NONE;
}

// ---------------------------------------------------------------- state
enum Screen { S_LIST, S_PAD, S_SETTINGS, S_PAIR };
static Screen screen = S_LIST;
static int sel = 0;              // selection in current list
static int padSlot = -1;         // detail screen: live slot, or -1 for an offline known controller
static int padKnown = -1;        // detail screen: index in the paired list
static uint32_t toastUntil = 0;
static char toast[32];
static uint32_t rumbleOffAt = 0;
static int rumbleSlot = -1;
static uint32_t pairDoneAt = 0;
static int knownAtPairStart = 0;
static uint8_t ledLevel[MAX_PADS];

static void say(const char *m) { strncpy(toast, m, sizeof(toast) - 1); toast[sizeof(toast) - 1] = 0; toastUntil = millis() + 2500; }

struct Row { int kind; int slot; int known; };   // kind: 0 live, 1 offline, 2 pair, 3 settings
static Row rows[MAX_PADS * 2 + 2];
static int nRows = 0;

static void buildRows() {
  nRows = 0;
  bool shown[MAX_PADS] = {false};
  for (int i = 0; i < MAX_PADS; i++) {
    const Pad &p = gip_pad(i);
    if (p.stage == PAD_FREE) continue;
    rows[nRows++] = {0, i, pairing_index(p.mac)};
    if (pairing_index(p.mac) >= 0) shown[pairing_index(p.mac)] = true;
  }
  for (int k = 0; k < pairing_count(); k++) if (!shown[k]) rows[nRows++] = {1, -1, k};
  rows[nRows++] = {2, -1, -1};
  rows[nRows++] = {3, -1, -1};
}

static const char *padName(const Pad &p) {
  switch (p.pid) {
    case 0x0b12: case 0x0b13: return "Series X|S";
    case 0x02e0: case 0x02fd: case 0x02ea: return "One S";
    case 0x0b00: return "Elite 2";
    case 0x02e3: return "Elite";
    default: return "Controller";
  }
}

// ---------------------------------------------------------------- drawing helpers
static void txt(int x, int y, const char *s, uint16_t c, int sz = 1) { cv->setTextSize(sz); cv->setTextColor(c); cv->setCursor(x, y); cv->print(s); }

static void drawBattery(int x, int y, const Pad &p) {
  cv->drawRect(x, y, 18, 9, C_DIM); cv->fillRect(x + 18, y + 2, 2, 5, C_DIM);
  if (!p.battKnown) { txt(x + 6, y + 1, "?", C_DIM); return; }
  int bars = p.battLevel + 1; if (bars > 4) bars = 4;
  uint16_t c = p.battLevel == 0 ? C_RED : (p.battLevel == 1 ? C_YEL : C_GRN);
  for (int i = 0; i < bars; i++) cv->fillRect(x + 2 + i * 4, y + 2, 3, 5, c);
}

static void drawSignal(int x, int y, int rssi) {
  int bars = rssi > -45 ? 4 : rssi > -55 ? 3 : rssi > -65 ? 2 : rssi > -75 ? 1 : 0;
  for (int i = 0; i < 4; i++) {
    int h = 3 + i * 2;
    if (i < bars) cv->fillRect(x + i * 4, y + 9 - h, 3, h, C_GRN); else cv->drawRect(x + i * 4, y + 9 - h, 3, h, C_DIM);
  }
}

static void header(const char *title) {
  cv->fillRect(0, 0, UI_W, 15, C_BLU);
  txt(4, 4, title, C_FG);
  const char *u = hid_mounted() ? (hid_suspended() ? "zz" : "USB") : "no";
  uint16_t uc = hid_mounted() ? (hid_suspended() ? C_YEL : C_GRN) : C_RED;
  txt(UI_W - 24, 4, u, uc);
}

static void footer(const char *s) { cv->fillRect(0, UI_H - 12, UI_W, 12, C_BG); txt(2, UI_H - 10, s, C_DIM); }

static void drawToast() {
  if (millis() < toastUntil) { cv->fillRect(8, UI_H / 2 - 12, UI_W - 16, 22, C_FG); txt(14, UI_H / 2 - 5, toast, C_BG); }
}

static void menuLine(int y, const char *s, bool selected, uint16_t c = C_FG) {
  if (selected) { cv->fillRect(0, y - 2, UI_W, 14, C_SEL); cv->drawFastVLine(0, y - 2, 14, C_ACC); }
  txt(8, y + 1, s, selected ? C_ACC : c);
}

// ---------------------------------------------------------------- screens
static void drawList() {
  header("XBOX BRIDGE");
  int visible = 5, first = sel >= visible ? sel - visible + 1 : 0;
  for (int v = 0; v < visible && first + v < nRows; v++) {
    int i = first + v, y = 19 + v * 27;
    Row &r = rows[i];
    bool s = (i == sel);
    if (s) { cv->fillRect(0, y - 2, UI_W, 26, C_SEL); cv->drawFastVLine(0, y - 2, 26, C_ACC); }
    if (r.kind == 0) {
      const Pad &p = gip_pad(r.slot);
      uint16_t dot = p.stage == PAD_READY ? C_GRN : C_YEL;
      cv->fillCircle(12, y + 10, 5, dot);
      char b[24]; snprintf(b, sizeof(b), "P%d %s", r.slot + 1, padName(p)); txt(24, y + 2, b, s ? C_ACC : C_FG);
      txt(24, y + 14, p.stage == PAD_READY ? "connected" : "connecting...", C_DIM);
      drawBattery(UI_W - 26, y + 2, p); drawSignal(UI_W - 26, y + 13, p.rssi);
    } else if (r.kind == 1) {
      cv->drawCircle(12, y + 10, 5, C_DIM);
      char b[24]; snprintf(b, sizeof(b), "Paired #%d", r.known + 1); txt(24, y + 2, b, C_DIM);
      txt(24, y + 14, "offline - press Xbox btn", C_DIM);
    } else if (r.kind == 2) {
      txt(10, y + 2, "+ Pair controller", s ? C_ACC : C_GRN);
      txt(10, y + 14, "click to start", C_DIM);
    } else {
      txt(10, y + 2, "Settings", s ? C_ACC : C_FG);
      char b[28]; snprintf(b, sizeof(b), "slots %d  ch%u", hid_slots(), radio_channel()); txt(10, y + 14, b, C_DIM);
    }
  }
  footer("hold knob 1s = pair");
}

static void drawStick(int cx, int cy, int x, int y, bool pressed) {
  cv->drawCircle(cx, cy, 20, pressed ? C_ACC : C_DIM);
  cv->fillCircle(cx + x * 16 / 32768, cy + y * 16 / 32768, 4, pressed ? C_ACC : C_FG);
}

static const char *padItems[] = {"Rumble test", "LED brightness", "Disconnect", "Forget controller", "Back"};

static void drawPad() {
  char title[24];
  if (padSlot >= 0) { const Pad &p = gip_pad(padSlot); snprintf(title, sizeof(title), "P%d %s", padSlot + 1, padName(p)); }
  else snprintf(title, sizeof(title), "Paired #%d (offline)", padKnown + 1);
  header(title);
  int y0 = 20;
  if (padSlot >= 0 && gip_pad(padSlot).stage == PAD_READY) {
    const PadInput &in = gip_pad(padSlot).in;
    drawStick(34, y0 + 22, in.lx, in.ly, in.buttons & GIP_BTN_L3);
    drawStick(UI_W - 34, y0 + 22, in.rx, in.ry, in.buttons & GIP_BTN_R3);
    cv->drawRect(62, y0, 14, 12, C_DIM); cv->fillRect(63, y0 + 11 - in.lt * 10 / 1023, 12, in.lt * 10 / 1023, C_ACC);
    cv->drawRect(UI_W - 76, y0, 14, 12, C_DIM); cv->fillRect(UI_W - 75, y0 + 11 - in.rt * 10 / 1023, 12, in.rt * 10 / 1023, C_ACC);
    struct { const char *n; uint16_t m; uint16_t c; int x; int y; } bt[] = {
      {"A", GIP_BTN_A, C_GRN, 80, y0 + 20}, {"B", GIP_BTN_B, C_RED, 92, y0 + 14}, {"X", GIP_BTN_X, C_BLU, 68, y0 + 14}, {"Y", GIP_BTN_Y, C_YEL, 80, y0 + 8}};
    for (auto &b : bt) { bool on = in.buttons & b.m; cv->drawCircle(b.x + 3, b.y + 3, 5, b.c); if (on) cv->fillCircle(b.x + 3, b.y + 3, 5, b.c); }
    txt(54, y0 + 32, (in.buttons & GIP_BTN_LB) ? "LB" : "  ", C_ACC); txt(104, y0 + 32, (in.buttons & GIP_BTN_RB) ? "RB" : "  ", C_ACC);
    cv->drawFastHLine(0, y0 + 48, UI_W, C_DIM);
  } else {
    txt(8, y0 + 8, padSlot >= 0 ? "connecting..." : "not connected", C_DIM);
  }
  int my = y0 + 54, n = padSlot >= 0 ? 5 : 2;
  for (int i = 0; i < n; i++) {
    const char *name = padSlot >= 0 ? padItems[i] : (i == 0 ? padItems[3] : padItems[4]);
    char b[28];
    if (padSlot >= 0 && i == 1) snprintf(b, sizeof(b), "LED brightness: %d", ledLevel[padSlot] ? ledLevel[padSlot] : 20); else snprintf(b, sizeof(b), "%s", name);
    menuLine(my + i * 16, b, sel == i, (padSlot >= 0 && i == 3) ? C_RED : C_FG);
  }
}

static void drawSettings() {
  header("SETTINGS");
  char b[32];
  snprintf(b, sizeof(b), "USB gamepads: %d", hid_slots());
  menuLine(24, b, sel == 0); txt(8, 40, "(restarts the bridge)", C_DIM);
  menuLine(58, "Forget ALL controllers", sel == 1, C_RED);
  menuLine(76, "Reboot to bootloader", sel == 2);
  menuLine(94, "Back", sel == 3);
  snprintf(b, sizeof(b), "paired %d  ch%u", pairing_count(), radio_channel()); txt(8, 120, b, C_DIM);
  txt(8, 134, "hold knob 10s = bootloader", C_DIM);
}

static void drawPairing() {
  header("PAIR CONTROLLER");
  uint32_t left = pairing_ms_left() / 1000;
  if (pairDoneAt) { txt(10, 50, "Paired!", C_GRN, 2); txt(10, 80, "Controller connecting...", C_FG); return; }
  txt(8, 24, "1. Turn the controller on", C_FG);
  txt(8, 38, "2. Hold the small PAIR", C_FG);
  txt(8, 50, "   button (top, by USB-C)", C_FG);
  txt(8, 62, "   until it blinks FAST", C_FG);
  txt(8, 80, "3. Keep it close to this", C_FG);
  txt(8, 92, "   device", C_FG);
  char b[20]; snprintf(b, sizeof(b), "%lus left", (unsigned long)left); txt(8, 112, b, C_YEL, 2);
  cv->fillRect(8, 134, UI_W - 16, 6, C_DIM); cv->fillRect(8, 134, (UI_W - 16) * (int)left / 60, 6, C_ACC);
  footer("click = cancel");
}

// ---------------------------------------------------------------- logic
static int listLen() {
  if (screen == S_LIST) return nRows;
  if (screen == S_PAD) return padSlot >= 0 ? 5 : 2;
  if (screen == S_SETTINGS) return 4;
  return 1;
}

static void doClick() {
  if (screen == S_LIST) {
    Row &r = rows[sel];
    if (r.kind == 0 || r.kind == 1) { screen = S_PAD; padSlot = r.kind == 0 ? r.slot : -1; padKnown = r.known; sel = 0; }
    else if (r.kind == 2) { knownAtPairStart = pairing_count(); pairDoneAt = 0; pairing_start(); screen = S_PAIR; }
    else { screen = S_SETTINGS; sel = 0; }
  } else if (screen == S_PAD) {
    if (padSlot >= 0) {
      switch (sel) {
        case 0: gip_rumble(padSlot, 70, 70, 0, 0); rumbleSlot = padSlot; rumbleOffAt = millis() + 700; leds_flash(255, 120, 0, 700); say("rumble!"); break;
        case 1: { uint8_t l = ledLevel[padSlot] ? ledLevel[padSlot] : 20; l = l >= 40 ? 0 : l + 10; if (l == 0 && ledLevel[padSlot] == 0) l = 10; ledLevel[padSlot] = l ? l : 1; gip_led(padSlot, l ? 1 : 0, l); break; }
        case 2: gip_disconnect(padSlot); screen = S_LIST; sel = 0; say("disconnected"); break;
        case 3: { const uint8_t *m = gip_pad(padSlot).mac; uint8_t mm[6]; memcpy(mm, m, 6); pairing_remove(mm); gip_disconnect(padSlot); screen = S_LIST; sel = 0; say("forgotten"); break; }
        default: screen = S_LIST; sel = 0;
      }
    } else {
      if (sel == 0) { const uint8_t *m = pairing_mac(padKnown); if (m) { uint8_t mm[6]; memcpy(mm, m, 6); pairing_remove(mm); } say("forgotten"); }
      screen = S_LIST; sel = 0;
    }
  } else if (screen == S_SETTINGS) {
    if (sel == 0) { hid_request_slots(hid_slots() % MAX_PADS + 1); say("restarting..."); leds_flash(170, 0, 255, 900); delay(900); ESP.restart(); }
    else if (sel == 1) { pairing_clear(); for (int i = 0; i < MAX_PADS; i++) gip_disconnect(i); say("all forgotten"); screen = S_LIST; sel = 0; }
    else if (sel == 2) { say("bootloader..."); delay(600); boot_to_download(); }
    else { screen = S_LIST; sel = 0; }
  } else if (screen == S_PAIR) { pairing_stop(); screen = S_LIST; sel = 0; }
}

static void uiTask(void *) {
  esp_task_wdt_add(NULL);
  for (;;) {
    esp_task_wdt_reset();
    buildRows();
    int d; noInterrupts(); d = encDelta; encDelta = 0; interrupts();
    int n = listLen();
    if (d && n) { sel = (sel + (d > 0 ? 1 : -1) + n) % n; }
    if (sel >= n) sel = 0;
    Btn b = pollButton();
    if (b == B_CLICK) doClick();
    if (pairing_active() && screen != S_PAIR) { screen = S_PAIR; pairDoneAt = 0; knownAtPairStart = pairing_count(); }   // knob hold started pairing
    if (rumbleOffAt && (int32_t)(millis() - rumbleOffAt) >= 0) { rumbleOffAt = 0; gip_rumble(rumbleSlot, 0, 0, 0, 0); }
    if (screen == S_PAIR) {
      if (pairing_count() > knownAtPairStart && !pairDoneAt) pairDoneAt = millis();
      if (pairDoneAt && millis() - pairDoneAt > 3000) { screen = S_LIST; sel = 0; pairDoneAt = 0; }
      else if (!pairDoneAt && !pairing_active()) { screen = S_LIST; sel = 0; }
    }
    if (screen == S_PAD && padSlot >= 0 && gip_pad(padSlot).stage == PAD_FREE) { screen = S_LIST; sel = 0; }

    cv->fillScreen(C_BG);
    switch (screen) {
      case S_LIST: drawList(); break;
      case S_PAD: drawPad(); break;
      case S_SETTINGS: drawSettings(); break;
      case S_PAIR: drawPairing(); break;
    }
    drawToast();
    tft.drawRGBBitmap(0, 0, cv->getBuffer(), UI_W, UI_H);
    vTaskDelay(pdMS_TO_TICKS(40));
  }
}

void ui_begin() {
  pinMode(PIN_PWR_EN, OUTPUT); digitalWrite(PIN_PWR_EN, HIGH);
  pinMode(PIN_CC1101_CS, OUTPUT); digitalWrite(PIN_CC1101_CS, HIGH);
  pinMode(PIN_SD_CS, OUTPUT); digitalWrite(PIN_SD_CS, HIGH);
  ledcSetup(0, 20000, 8); ledcAttachPin(PIN_LCD_BL, 0); ledcWrite(0, 90);   // dimmed backlight (~35%) to save power
  pinMode(PIN_ENC_A, INPUT_PULLUP); pinMode(PIN_ENC_B, INPUT_PULLUP); pinMode(PIN_ENC_KEY, INPUT_PULLUP);
  attachInterrupt(PIN_ENC_A, encIsr, FALLING);
  delay(30);
  SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, -1);
  tft.init(170, 320);
  tft.setSPISpeed(40000000);
  tft.setRotation(1);
  tft.fillScreen(C_BG);
  cv = new GFXcanvas16(UI_W, UI_H);
  xTaskCreatePinnedToCore(uiTask, "ui", 6144, nullptr, 1, nullptr, 1);
}

void ui_notify_pairing_done() {}

void ui_off() { ledcWrite(0, 0); }
