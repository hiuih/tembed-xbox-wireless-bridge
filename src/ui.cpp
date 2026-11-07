// T-Embed UI: display, navigation, and live data. Drawing is in ui_screens*.cpp (pure, also run by tools/sim).
#include "config.h"
#if HAS_SCREEN
#include "ui.h"
#include <SPI.h>
#include <Adafruit_ST7789.h>
#include "esp_task_wdt.h"
#include "esp_mac.h"
#include "ui_screens.h"
#include "settings.h"
#include "stats.h"
#include "nrf24.h"
#include "encoder.h"
#include "knob.h"
#include "powerbtn.h"
#include "pairing.h"
#include "gip.h"
#include "hid.h"
#include "leds.h"
#include "power.h"
#include "bootguard.h"
#include "radio.h"
#include "evlog.h"
#include "dlog.h"
#include "ble.h"

#define BAND_H 85
#define FRAME_MS 50

static Adafruit_ST7789 tft(&SPI, PIN_LCD_CS, PIN_LCD_DC, -1);
static GFXcanvas16 *band = nullptr;
static Snap snap;
static Nav nav;
static int liveSlot[MAX_PADS];            // snapshot index (paired-list index) -> live GIP slot, -1 if offline
static uint32_t lastActivity = 0, lastStepMs = 0, rumbleOffAt = 0;
static int rumbleSlot = -1;
static uint8_t ledLevelPad[MAX_PADS];
static PowerInfo power;

// The UI runs on core 0; the protocol (radio TX, pairing list, GIP) belongs to core 1. UI actions are posted here
// and executed by ui_pump() from loop(), so the two cores never touch protocol state at the same time.
enum { C_RUMBLE, C_LED, C_DISC, C_FORGET_ONE, C_FORGET_ALL, C_PAIR_TOGGLE };
struct Cmd { uint8_t t; int8_t slot; uint8_t a, b; uint8_t mac[6]; };
static QueueHandle_t cmdQ = nullptr;
static void post(uint8_t t, int slot = 0, uint8_t a = 0, uint8_t b = 0, const uint8_t *mac = nullptr) {
  Cmd c{}; c.t = t; c.slot = slot; c.a = a; c.b = b; if (mac) memcpy(c.mac, mac, 6);
  if (cmdQ) xQueueSend(cmdQ, &c, 0);
}
void ui_pump() {
  Cmd c;
  while (cmdQ && xQueueReceive(cmdQ, &c, 0) == pdTRUE) {
    switch (c.t) {
      case C_RUMBLE: gip_rumble(c.slot, c.a, c.a, 0, 0); break;
      case C_LED: gip_led(c.slot, 1, c.a); break;
      case C_DISC: gip_disconnect(c.slot); break;
      case C_FORGET_ONE: pairing_remove(c.mac); if (c.slot >= 0) gip_disconnect(c.slot); break;
      case C_FORGET_ALL: for (int i = 0; i < MAX_PADS; i++) gip_disconnect(i); pairing_clear(); break;
      case C_PAIR_TOGGLE: if (pairing_active()) pairing_stop(); else pairing_start(settings_pair_ms()); break;
    }
  }
}
static uint32_t lastPower = 0;
static Screen prevScreen = SC_HOME;
static_assert(EVLOG_N == UI_LOG_N && EVLOG_LEN == UI_LOG_LEN, "event log sizes must match the UI snapshot");

// ---------------------------------------------------------------- helpers
static const char *padName(uint16_t pid) {
  switch (pid) {
    case 0x0b12: case 0x0b13: return "Series X|S";
    case 0x02e0: case 0x02fd: case 0x02ea: return "One S";
    case 0x0b00: return "Elite 2";
    case 0x02e3: return "Elite";
    default: return "Xbox controller";
  }
}

static void toast(const char *m) {
  strncpy(nav.toast, m, sizeof(nav.toast) - 1);
  nav.toast[sizeof(nav.toast) - 1] = 0;
  nav.toastUntilMs = millis() + 1800;
}

static void goTo(Screen sc, int sel = 0) {
  prevScreen = nav.screen;
  nav.screen = sc; nav.sel = sel; nav.scroll = 0; nav.scrollAnim = 0; nav.confirm = false;
  nav.screenSinceMs = millis();
  if (sc == SC_HOME) { nav.selX = 8 + (sel % 3) * 104; nav.selY = 28 + (sel / 3) * 64; nav.scrollAnim = (sel / 3) > 1 ? (sel / 3) - 1 : 0; }
}

static void setBacklight(uint8_t idx) {
  static const uint8_t duty[5] = {30, 70, 120, 190, 255};
  ledcWrite(0, duty[idx % 5]);
}

// ---------------------------------------------------------------- live data
static void trackEvents();
static void fillSnap() {
  uint32_t now = millis();
  snap.nowMs = now;
  snap.uptimeS = now / 1000;
  snap.usbMounted = hid_mounted();
  snap.usbSuspended = hid_suspended();
  snap.gamepads = hid_slots();
  snap.gamepadsMax = hid_max_slots();
  snap.style = xinput_style();
  snap.pairing = pairing_active();
  snap.pairLeftMs = pairing_ms_left();
  snap.channel = radio_channel();
  snap.scanning = radio_scanning();
  snap.nKnown = pairing_count();
  snap.pmicOk = power.vbat > 0;
  snap.vbusPresent = power.vbusPresent; snap.charging = power.charging;
  snap.battPct = power.pct; snap.vbat = power.vbat; snap.vbus = power.vbus; snap.vsys = power.vsys; snap.chgmA = power.chgmA;
  stats_fill(snap);
  snap.nrfOk = nrf24_ok();
  if (snap.nrfOk) { memcpy(snap.spec, nrf24_occupancy(), 84); memcpy(snap.peak, nrf24_peak(), 84); }
  snap.brightness = cfg.brightness; snap.ledLevel = cfg.ledLevel; snap.sleepMin = cfg.sleepMin;
  snap.accent = cfg.accent; snap.reverseKnob = cfg.reverseKnob;
  snap.fwVersion = "1.0";
  snap.bleOn = cfg.bleOn; snap.bleState = ble_state();
  snap.autoDim = cfg.autoDim; snap.ringFx = cfg.ringFx; snap.deadzone = cfg.deadzone; snap.pairWin = cfg.pairWin;
  snap.flip = cfg.flip; snap.wakePc = cfg.wakePc; snap.pairBoot = cfg.pairBoot; snap.pairTotalMs = settings_pair_ms();
  snap.logN = evlog_copy(snap.logMsg, snap.logAtS);
  trackEvents();
  snap.sleepWhy = powerbtn_last_sleep(); snap.pmicFault = power_fault(); snap.resetReason = boot_reason(); snap.prevMark = boot_prev_mark(); snap.prevUiMark = boot_prev_ui_mark();
  snap.uiStackFree = uxTaskGetStackHighWaterMark(nullptr) * sizeof(StackType_t);

  // controllers = the paired list; a live slot with the same MAC supplies the live data
  snap.nReady = 0;
  for (int k = 0; k < MAX_PADS; k++) {
    PadView &v = snap.pad[k];
    liveSlot[k] = -1;
    if (k >= pairing_count()) { v.present = false; continue; }
    const uint8_t *mac = pairing_mac(k);
    for (int i = 0; i < MAX_PADS; i++) {
      const Pad &p = gip_pad(i);
      if (p.stage != PAD_FREE && !memcmp(p.mac, mac, 6)) { liveSlot[k] = i; break; }
    }
    v.present = true;
    if (liveSlot[k] < 0) { v.online = false; v.connecting = false; strcpy(v.name, "Xbox controller"); v.rssi = -99; v.rateHz = 0; continue; }
    const Pad &p = gip_pad(liveSlot[k]);
    v.online = p.stage == PAD_READY;
    v.connecting = !v.online;
    if (v.online) snap.nReady++;
    strncpy(v.name, padName(p.pid), sizeof(v.name) - 1); v.name[sizeof(v.name) - 1] = 0;
    v.rssi = p.rssi;
    v.battKnown = p.battKnown; v.battType = p.battType; v.battLevel = p.battLevel;
    v.in = p.in;
    memcpy(v.gap, p.inHist, sizeof(v.gap)); v.gipSeen = p.gipSeen; v.gipMissed = p.inputGaps;
    v.rateHz = stats_pad_rate(liveSlot[k]);
    uint32_t tot = p.inputGaps + p.gipSeen;
    v.lossPermille = tot ? (uint16_t)(p.inputGaps * 1000UL / tot) : 0;
    v.upS = (now - p.connectedAt) / 1000;
  }
}

// Turns state changes into event-log lines.
static void trackEvents() {
  static bool first = true, pairing = false, mounted = false, suspended = false;
  static int gamepads = -1;
  if (first) { first = false; pairing = snap.pairing; mounted = snap.usbMounted; suspended = snap.usbSuspended; gamepads = snap.gamepads; return; }
  if (snap.pairing != pairing) { pairing = snap.pairing; evlog_add(pairing ? "!Pairing opened" : "Pairing closed"); }
  if (snap.usbMounted != mounted) { mounted = snap.usbMounted; if (snap.gamepads > 0) evlog_add(mounted ? "+USB connected" : "-USB unplugged"); }
  if (snap.usbSuspended != suspended) { suspended = snap.usbSuspended; evlog_add(suspended ? "!Computer asleep" : "Computer awake"); }
  if (snap.gamepads != gamepads) { gamepads = snap.gamepads; evlog_add("USB: %d pad%s emulated", gamepads, gamepads == 1 ? "" : "s"); }
}

static void updateTrails() {
  for (int k = 0; k < MAX_PADS; k++) {
    PadView &v = snap.pad[k];
    if (!v.present || !v.online) { memset(v.trail, 0, sizeof(v.trail)); continue; }
    memmove(&v.trail[1], &v.trail[0], sizeof(v.trail) - sizeof(v.trail[0]));
    v.trail[0][0] = v.in.lx * 100 / 32768; v.trail[0][1] = v.in.ly * 100 / 32768;
    v.trail[0][2] = v.in.rx * 100 / 32768; v.trail[0][3] = v.in.ry * 100 / 32768;
  }
}

// ---------------------------------------------------------------- navigation
// Fast spinning clamps at the ends instead of wrapping, so it never "bounces"; a single slow click wraps.
static void moveSel(int steps, int count) {
  if (!steps || count <= 0) return;
  uint32_t now = millis();
  bool fast = abs(steps) > 1 || now - lastStepMs < 250;
  lastStepMs = now;
  int ns = nav.sel + steps;
  if (ns < 0) ns = fast ? 0 : count - 1;
  if (ns >= count) ns = fast ? count - 1 : 0;
  nav.sel = ns;
}

static int padRows() { int n = 0; for (int i = 0; i < MAX_PADS; i++) if (snap.pad[i].present) n++; return n; }
static int padIndexOfRow(int row) { int n = 0; for (int i = 0; i < MAX_PADS; i++) if (snap.pad[i].present) { if (n == row) return i; n++; } return -1; }

static void forgetAll() {
  post(C_FORGET_ALL);
  toast("All controllers forgotten");
}

static void applyAllSettings() {
  setBacklight(cfg.brightness); theme_set(cfg.accent); leds_set_level(cfg.ledLevel); leds_set_idle_fx(cfg.ringFx);
  encoder_set_reverse(cfg.reverseKnob); tft.setRotation(cfg.flip ? 3 : 1);
}

static void settingsClick() {
  static const uint8_t sleepOpts[4] = {0, 5, 15, 30};
  switch (nav.sel) {
    case 0: cfg.brightness = (cfg.brightness + 1) % 5; setBacklight(cfg.brightness); break;
    case 1: cfg.autoDim = (cfg.autoDim + 1) % 4; break;
    case 2: { int i = 0; for (; i < 4; i++) if (sleepOpts[i] == cfg.sleepMin) break; cfg.sleepMin = sleepOpts[(i + 1) % 4]; break; }
    case 3: cfg.accent = (cfg.accent + 1) % 4; theme_set(cfg.accent); break;
    case 4: cfg.ledLevel = (cfg.ledLevel + 1) % 4; leds_set_level(cfg.ledLevel); break;
    case 5: cfg.ringFx = (cfg.ringFx + 1) % 3; leds_set_idle_fx(cfg.ringFx); break;
    case 6: cfg.flip = !cfg.flip; tft.setRotation(cfg.flip ? 3 : 1); break;
    case 7: cfg.reverseKnob = !cfg.reverseKnob; encoder_set_reverse(cfg.reverseKnob); break;
    case 8: xinput_set_style(1 - xinput_style()); { char m[40]; snprintf(m, sizeof(m), "USB: %s", usbModeName(1 - snap.style)); toast(m); } evlog_add("USB mode: %s", usbModeName(1 - snap.style)); return;
    case 9: cfg.bleOn = !cfg.bleOn; settings_save(); toast("Restarting for Bluetooth..."); dlog("Bluetooth %s: restarting", cfg.bleOn ? "on" : "off"); delay(900); ESP.restart(); return;
    case 10: cfg.deadzone = (cfg.deadzone + 1) % 6; break;
    case 11: cfg.wakePc = !cfg.wakePc; break;
    case 12: cfg.pairWin = (cfg.pairWin + 1) % 4; break;
    case 13: cfg.pairBoot = !cfg.pairBoot; break;
    case 14: if (nav.confirm) { bool ble = cfg.bleOn; settings_reset(); applyAllSettings(); nav.confirm = false; toast(ble ? "Reset (Bluetooth: restart)" : "Settings reset"); } else nav.confirm = true; return;
    case 15: if (nav.confirm) { nav.confirm = false; toast(ble_forget_hosts() ? "Bluetooth hosts forgotten" : "Turn Bluetooth on first"); } else nav.confirm = true; return;
    case 16: if (nav.confirm) { forgetAll(); nav.confirm = false; evlog_add("-All controllers forgotten"); } else nav.confirm = true; return;
    case 17: if (nav.confirm) { toast("Bootloader..."); delay(500); boot_to_download(); } else nav.confirm = true; return;
    case 18: goTo(SC_ABOUT); return;
  }
  settings_save();
}

static void padAction() {
  int k = nav.padSlot, ls = (k >= 0 && k < MAX_PADS) ? liveSlot[k] : -1;
  switch (nav.sel) {
    case 0: if (ls >= 0) { post(C_RUMBLE, ls, 70); rumbleSlot = ls; rumbleOffAt = millis() + 700; toast("Rumble sent"); } break;
    case 1: if (ls >= 0) { uint8_t l = ledLevelPad[ls] ? ledLevelPad[ls] : 20; l = l >= 40 ? 10 : l + 10; ledLevelPad[ls] = l; post(C_LED, ls, l); toast("LED changed"); } break;
    case 2: if (ls >= 0) { post(C_DISC, ls); toast("Disconnected"); goTo(SC_PADS); } break;
    case 3:
      if (!nav.confirm) { nav.confirm = true; toast("Click again to forget"); break; }
      { uint8_t mm[6] = {0}; const uint8_t *m = pairing_mac(k); if (m) { memcpy(mm, m, 6); post(C_FORGET_ONE, ls, 0, 0, mm); } toast("Forgotten"); goTo(SC_PADS); }
      break;
  }
}

static void handleInput() {
  int steps = encoder_take();
  bool click = knob_take_click();
  bool back = powerbtn_take_back();
  if (steps || click || back) {
    lastActivity = millis();
    dlog("IN steps=%d click=%d back=%d screen=%d sel=%d ready=%d", steps, (int)click, (int)back, (int)nav.screen, nav.sel, snap.nReady);
  }

  if (back) {
    switch (nav.screen) {
      case SC_PAD: goTo(SC_PADS); break;
      case SC_ABOUT: if (prevScreen == SC_SETTINGS) goTo(SC_SETTINGS, 18); else { goTo(SC_HOME); nav.sel = ui_home_index(SC_ABOUT); nav.selX = 8 + (nav.sel % 3) * 104; nav.selY = 28 + (nav.sel / 3) * 64; nav.scrollAnim = (nav.sel / 3) > 1 ? (nav.sel / 3) - 1 : 0; } break;
      case SC_HOME: break;
      default: { int from = nav.screen; goTo(SC_HOME); nav.sel = ui_home_index((Screen)from); nav.selX = 8 + (nav.sel % 3) * 104; nav.selY = 28 + (nav.sel / 3) * 64; nav.scrollAnim = (nav.sel / 3) > 1 ? (nav.sel / 3) - 1 : 0; }
    }
    return;
  }
  switch (nav.screen) {
    case SC_HOME:
      moveSel(steps, HOME_TILES);
      if (click) goTo(ui_home_screen(nav.sel));
      break;
    case SC_PADS: {
      int rows = padRows() + 1;
      moveSel(steps, rows);
      if (click) {
        if (nav.sel < rows - 1) { int k = padIndexOfRow(nav.sel); goTo(SC_PAD, 0); nav.padSlot = k; }
        else goTo(SC_PAIR);
      }
      break;
    }
    case SC_PAD: if (steps) nav.confirm = false; moveSel(steps, 4); if (click) padAction(); break;
    case SC_PAIR: if (click) post(C_PAIR_TOGGLE); break;
    case SC_POWER: if (click) powerbtn_sleep_now(); break;
    case SC_SETTINGS: if (steps) nav.confirm = false; moveSel(steps, SETTINGS_ROWS); if (click) settingsClick(); break;
    case SC_LAG: { int c = 0; for (int i = 0; i < MAX_PADS; i++) if (snap.pad[i].present && snap.pad[i].online) c++; moveSel(steps, c > 0 ? c : 1); break; }
    case SC_LOG: moveSel(steps, snap.logN > 0 ? snap.logN : 1); break;
    default: break;
  }
}

static void animate() {
  nav.selX += ((8 + (nav.sel % 3) * 104) - nav.selX) * 0.4f;
  nav.selY += ((28 + (nav.sel / 3) * 64) - nav.selY) * 0.4f;
  if (nav.screen == SC_HOME) { float tg = (nav.sel / 3) > 1 ? (nav.sel / 3) - 1 : 0; nav.scrollAnim += (tg - nav.scrollAnim) * 0.35f; return; }
  float target = 0;
  if (nav.screen == SC_PADS) { int rows = padRows() + 1; target = nav.sel > 2 ? nav.sel - 2 : 0; if (rows <= 3) target = 0; }
  if (nav.screen == SC_SETTINGS) target = nav.sel > 4 ? nav.sel - 4 : 0;
  nav.scrollAnim += (target - nav.scrollAnim) * 0.35f;
}

// ---------------------------------------------------------------- task
static void uiTask(void *) {
  esp_task_wdt_add(NULL);
  uint32_t bootMs = millis();
  for (;;) {
    uint32_t t0 = millis();
    esp_task_wdt_reset();
    boot_ui_mark(1);
    if (nav.screen == SC_SPLASH && t0 - bootMs > 1700) goTo(SC_HOME);

    if (t0 - lastPower > 1000) { lastPower = t0; power_read(power); }
    fillSnap();
    updateTrails();
    if (nav.screen != SC_SPLASH) handleInput();
    if (rumbleOffAt && (int32_t)(t0 - rumbleOffAt) >= 0) { rumbleOffAt = 0; post(C_RUMBLE, rumbleSlot, 0); }
    if (snap.nReady > 0) lastActivity = millis();
    // idle: dim after a minute, sleep after the configured time with no controller
    // idle time is measured from a fresh millis(): t0 is older than an input stamped later in this same pass, and
    // t0 - lastActivity then wraps to ~4 billion ms, which used to put the board to sleep on every first input
    uint32_t idle = millis() - lastActivity;
    if (cfg.sleepMin && idle > (uint32_t)cfg.sleepMin * 60000UL && snap.nReady == 0) { dlog("AUTO-SLEEP idle=%lu limitMin=%u ready=%d", (unsigned long)idle, cfg.sleepMin, snap.nReady); powerbtn_sleep_now(SLEEP_AUTO); }
    { static uint32_t hb = 0; if (t0 - hb >= 5000) { hb = t0; dlog("HB heap=%lu min=%lu uiStack=%u loopMax=%lu usb=%d/%d pads=%d ready=%d vbat=%.2f scr=%d", (unsigned long)snap.perf.heapFree, (unsigned long)snap.perf.heapMin, (unsigned)snap.uiStackFree, (unsigned long)snap.perf.loopMaxUs, (int)snap.usbMounted, (int)snap.usbSuspended, snap.gamepads, snap.nReady, snap.vbat, (int)nav.screen); } }
    { uint32_t dim = settings_dim_ms(); setBacklight(dim && idle > dim && snap.nReady == 0 ? 0 : cfg.brightness); }
    animate();
    boot_ui_mark(2);

    if (nav.screen == SC_SPECTRUM) nrf24_scan_slice(9000);

    for (int b = 0; b < 2; b++) {
      boot_ui_mark(3 + b);
      G g{band, b * BAND_H};
      ui_render(g, snap, nav);
      tft.drawRGBBitmap(0, b * BAND_H, band->getBuffer(), 320, BAND_H);
    }
    uint32_t used = millis() - t0;
    vTaskDelay(pdMS_TO_TICKS(used >= FRAME_MS ? 1 : FRAME_MS - used));
  }
}

void ui_begin() {
  pinMode(PIN_PWR_EN, OUTPUT); digitalWrite(PIN_PWR_EN, HIGH);
  pinMode(PIN_CC1101_CS, OUTPUT); digitalWrite(PIN_CC1101_CS, HIGH);
  pinMode(PIN_SD_CS, OUTPUT); digitalWrite(PIN_SD_CS, HIGH);
  ledcSetup(0, 20000, 8); ledcAttachPin(PIN_LCD_BL, 0);
  settings_begin();
  theme_set(cfg.accent);
  setBacklight(cfg.brightness);
  encoder_begin();
  encoder_set_reverse(cfg.reverseKnob);
  leds_set_level(cfg.ledLevel);
  delay(30);
  SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, -1);
  tft.init(170, 320);
  tft.setSPISpeed(40000000);
  tft.setRotation(cfg.flip ? 3 : 1);
  tft.fillScreen(0);
  leds_set_idle_fx(cfg.ringFx);
  evlog_add("boot: %s", boot_reason());
  if (cfg.pairBoot) pairing_start(settings_pair_ms());
  nrf24_begin();
  band = new GFXcanvas16(320, BAND_H);
  esp_read_mac(snap.mac, ESP_MAC_WIFI_STA);
  lastActivity = millis();
  goTo(SC_SPLASH);
  // low priority on core 0 (alongside Wi-Fi): drawing can never delay the protocol loop on core 1
  cmdQ = xQueueCreate(8, sizeof(Cmd));
  xTaskCreatePinnedToCore(uiTask, "ui", 16384, nullptr, 1, nullptr, 0);
}

void ui_off() { ledcWrite(0, 0); }
#endif
