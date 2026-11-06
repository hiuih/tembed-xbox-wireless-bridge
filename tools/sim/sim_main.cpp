// Renders every screen to raw RGB565 files using the real UI drawing code, with fake data.
#include <stdio.h>
#include <string.h>
#include <string>
#include "ui_screens.h"

static Snap snap;
static Nav nav;

static void fillSnap(int pads) {
  memset(&snap, 0, sizeof(snap));
  snap.nowMs = 5200; snap.uptimeS = 4210;
  snap.usbMounted = true; snap.gamepads = pads > 1 ? 2 : 1; snap.gamepadsMax = 4; snap.style = 0;
  snap.nReady = pads; snap.nKnown = 3;
  snap.channel = 6; snap.pmicOk = true; snap.vbusPresent = true; snap.charging = false; snap.battPct = 72;
  snap.vbat = 3.98f; snap.vbus = 5.1f; snap.vsys = 4.12f;
  snap.perf = {412, 236, 0, 3, 310, 3400, 98000, 61000, 0};
  snap.fwVersion = "0.2"; snap.brightness = 3; snap.ledLevel = 2; snap.sleepMin = 15; snap.accent = 0;
  const uint8_t mac[6] = {0x90, 0x70, 0x69, 0x0a, 0x0f, 0x90}; memcpy(snap.mac, mac, 6);
  snap.histFill = UI_HIST;
  for (int i = 0; i < UI_HIST; i++) {
    snap.rateHist[0][i] = 90 + 30 * sinf(i * 0.3f) + (i % 7) * 2;
    snap.rateHist[1][i] = 70 + 25 * sinf(i * 0.21f + 1);
    snap.loopHist[i] = 1500 + 1200 * (1 + sinf(i * 0.5f)) + ((i % 11 == 0) ? 5000 : 0);
    snap.vbatHist[i] = 4120 - i * 2 + (int)(15 * sinf(i * 0.4f));
  }
  snap.nrfOk = true;
  for (int i = 0; i < 84; i++) {
    float wifi = 0;
    for (int c : {12, 37, 62}) { float d = (i - c) / 7.0f; wifi += 70 * expf(-d * d); }
    snap.spec[i] = (uint8_t)std::min(100.0f, wifi + 6 + (i * 7 % 13));
    snap.peak[i] = std::min<int>(100, snap.spec[i] + 12 + (i % 5) * 3);
  }
  for (int i = 0; i < pads && i < 2; i++) {
    PadView &p = snap.pad[i];
    p.present = true; p.online = true;
    snprintf(p.name, sizeof(p.name), "Series X|S");
    p.rssi = i ? -63 : -41; p.battKnown = true; p.battLevel = i ? 1 : 3; p.rateHz = i ? 64 : 112; p.lossPermille = i ? 142 : 31; p.upS = 300;
    for (int a = 0; a < 8; a++) { p.trail[a][0] = 40 - a * 5; p.trail[a][1] = -30 + a * 4; p.trail[a][2] = -20 + a * 2; p.trail[a][3] = 25 - a * 3; }
  }
  snap.pad[0].in = {GIP_BTN_A | GIP_BTN_RB | GIP_BTN_DUP, 600, 120, 14000, -9000, -5000, 11000, false, false};
  if (pads < 3) { PadView &p = snap.pad[2]; p.present = true; p.online = false; snprintf(p.name, sizeof(p.name), "Series X|S"); p.rssi = -70; }
  if (pads == 0) { snap.pad[2].present = false; }
}

static void setNav(Screen sc, int sel = 0, float scrollAnim = 0, int padSlot = 0) {
  memset(&nav, 0, sizeof(nav));
  nav.screen = sc; nav.sel = sel; nav.scrollAnim = scrollAnim; nav.padSlot = padSlot;
  nav.selX = 8 + (sel % 3) * 104; nav.selY = 28 + (sel / 3) * 64;
  nav.screenSinceMs = 0;
}

static void render(const char *name) {
  static uint16_t full[320 * 170];
  for (int band = 0; band < 2; band++) {
    GFXcanvas16 c(320, 85);
    G g{&c, band * 85};
    ui_render(g, snap, nav);
    memcpy(full + band * 85 * 320, c.getBuffer(), 320 * 85 * 2);
  }
  char path[128]; snprintf(path, sizeof(path), "out/%s.raw", name);
  FILE *f = fopen(path, "wb"); fwrite(full, 2, 320 * 170, f); fclose(f);
}

int main(int argc, char **argv) {
  int accent = argc > 1 ? atoi(argv[1]) : 0;
  theme_set(accent);
  fillSnap(2);
  setNav(SC_HOME, 0); render("home");
  setNav(SC_HOME, 4); render("home_sel4");
  setNav(SC_PADS, 0); render("pads");
  setNav(SC_PAD, 1); render("pad");
  snap.pairing = true; snap.pairLeftMs = 41000; setNav(SC_PAIR); render("pair_on");
  snap.pairing = false; render("pair_off");
  setNav(SC_RADIO); render("radio");
  setNav(SC_SPECTRUM); render("spectrum");
  snap.nrfOk = false; render("spectrum_none"); snap.nrfOk = true;
  setNav(SC_POWER, 0); render("power");
  setNav(SC_SETTINGS, 2); render("settings");
  setNav(SC_SETTINGS, 7, 3.5f); nav.confirm = true; render("settings_danger");
  setNav(SC_ABOUT); render("about");
  snap.nowMs = 900; setNav(SC_SPLASH); render("splash");
  fillSnap(0); snap.nowMs = 5200; setNav(SC_PADS); render("pads_empty");
  fillSnap(2); snap.pairing = true; snap.pairLeftMs = 12000; setNav(SC_HOME, 1); render("home_pairing");
  fillSnap(2); setNav(SC_PAD, 0); snprintf(nav.toast, sizeof(nav.toast), "Rumble sent"); nav.toastUntilMs = 99999; render("pad_toast");
  return 0;
}
