#include "ui_screens.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static void wipe2(G &, const Snap &, const Nav &) {}   // screen transitions are applied after drawing (ui_render)

static void stat(G &g, int x, int y, const char *label, const char *val, col_t vk) {
  g.text(x, y, label, T.dim, FONT_S);
  g.text(x, y + 21, val, vk, FONT_B);
}

// ---------------------------------------------------------------- radio
void scr_radio(G &g, const Snap &s, const Nav &n) {
  ui_chrome(g, s, n, "Radio", "key: back");
  card(g, 8, 28, 150, 124, false);
  char b[32];
  snprintf(b, sizeof(b), "CH %u", s.channel);
  g.text(18, 54, b, T.accent, FONT_XL);
  snprintf(b, sizeof(b), "%u MHz%s", 2407 + 5 * s.channel, s.scanning ? "  scanning" : "");
  g.text(18, 61, b, T.dim, FONT_S);
  const PerfView &p = s.perf;
  snprintf(b, sizeof(b), "%lu/s", (unsigned long)p.rxPerSec);          stat(g, 18, 80, "FRAMES HEARD", b, T.text);
  snprintf(b, sizeof(b), "%lu/s", (unsigned long)p.evPerSec);          stat(g, 96, 80, "EVENTS", b, T.text);
  snprintf(b, sizeof(b), "%lu", (unsigned long)p.queueDrops);          stat(g, 18, 108, "QUEUE DROPS", b, p.queueDrops ? T.bad : T.good);
  snprintf(b, sizeof(b), "%lu.%lu ms", (unsigned long)(p.loopMaxUs / 1000), (unsigned long)(p.loopMaxUs % 1000 / 100));
  stat(g, 96, 108, "LOOP WORST", b, p.loopMaxUs > 8000 ? T.warn : T.good);
  snprintf(b, sizeof(b), "%luk free", (unsigned long)(p.heapFree / 1024));
  g.text(18, 141, b, T.dim, FONT_S);
  snprintf(b, sizeof(b), "tx err %lu", (unsigned long)p.txErr);
  g.textR(148, 141, b, p.txErr ? T.warn : T.dim, FONT_S);

  card(g, 166, 28, 146, 124, false);
  g.text(174, 38, "INPUT RATE  (60 s)", T.dim, FONT_S);
  int n0 = s.histFill < 2 ? 2 : s.histFill;
  const uint16_t *h0 = s.rateHist[0] + (UI_HIST - n0), *h1 = s.rateHist[1] + (UI_HIST - n0);
  g.hline(174, 50 + 46, 130, T.border);
  g.hline(174, 50 + 23, 130, mix(T.card, T.border, 100));
  sparkline(g, 174, 50, 130, 47, h0, n0, 0, 150, T.accent, true);
  sparkline(g, 174, 50, 130, 47, h1, n0, 0, 150, T.blue, false);
  g.text(174, 100, "0", T.dim, FONT_S);
  g.textR(304, 50, "150 Hz", T.dim, FONT_S);
  g.text(174, 108, "WORST LOOP  (60 s)", T.dim, FONT_S);
  sparkline(g, 174, 119, 130, 24, s.loopHist + (UI_HIST - n0), n0, 0, 12000, T.warn, true);
  wipe2(g, s, n);
}

// ---------------------------------------------------------------- spectrum
void scr_spectrum(G &g, const Snap &s, const Nav &n) {
  ui_chrome(g, s, n, "Spectrum  2.4 GHz", "nRF24 channel occupancy   key: back");
  card(g, 8, 28, 304, 124, false);
  if (!s.nrfOk) {
    icoSpectrum(g, 160, 76, T.dim);
    g.textC(160, 108, "nRF24 module not found", T.text, FONT_B);
    g.textC(160, 124, "expects CE on GPIO43, CSN on GPIO44", T.dim, FONT_S);
    wipe2(g, s, n);
    return;
  }
  const int X0 = 14, W = 292, Y0 = 36, H = 88, N = 84;
  // wifi channel bands (1, 6, 11), our channel highlighted
  static const uint8_t cc[3] = {1, 6, 11};
  for (int i = 0; i < 3; i++) {
    int centre = 5 * cc[i] + 7;                          // nRF channel index (MHz - 2400)
    int x0 = X0 + (centre - 10) * W / N, x1 = X0 + (centre + 10) * W / N;
    bool ours = s.channel == cc[i];
    g.rect(x0, Y0, x1 - x0, H, ours ? mix(T.card, T.accent, 38) : mix(T.card, T.border, 70));
    char b[4]; snprintf(b, sizeof(b), "%d", cc[i]);
    g.textC((x0 + x1) / 2, Y0 + H + 14, b, ours ? T.accent : T.dim, FONT_S);
  }
  for (int q = 1; q < 4; q++) g.hline(X0, Y0 + H - q * H / 4, W, mix(T.card, T.border, 90));
  for (int i = 0; i < N; i++) {
    int x = X0 + i * W / N;
    int h = s.spec[i] * H / 100;
    col_t k = mix(T.good, T.bad, s.spec[i] * 255 / 100);
    if (h > 0) g.rect(x, Y0 + H - h, 3, h, k);
    int ph = s.peak[i] * H / 100;
    if (ph > 1) g.rect(x, Y0 + H - ph, 3, 1, T.text);
  }
  g.text(18, 38, "100%", T.dim, FONT_S);
  wipe2(g, s, n);
}

// ---------------------------------------------------------------- power
void scr_power(G &g, const Snap &s, const Nav &n) {
  ui_chrome(g, s, n, "Power", "click: sleep now    key: back");
  card(g, 8, 28, 150, 78, false);
  col_t k = s.battPct < 15 ? T.bad : s.battPct < 35 ? T.warn : T.good;
  g.rframe(18, 38, 104, 36, 7, T.dim);
  g.rect(122, 48, 4, 16, T.dim);
  int w = 96 * s.battPct / 100;
  if (w > 0) g.rrect(22, 42, w, 28, 4, k);
  char b[24];
  if (s.pmicOk) { snprintf(b, sizeof(b), "%d%%", s.battPct); g.textC(70, 64, b, T.text, FONT_L); }
  else g.textC(70, 64, "--", T.dim, FONT_L);
  snprintf(b, sizeof(b), "%.2f V", s.vbat);
  g.text(18, 98, b, T.text, FONT_B);
  g.textR(150, 98, s.charging ? "charging" : s.vbusPresent ? "on USB" : "battery", s.charging ? T.good : T.dim, FONT_M);

  card(g, 166, 28, 146, 124, false);
  g.text(174, 38, "BATTERY VOLTAGE", T.dim, FONT_S);
  int n0 = s.histFill < 2 ? 2 : s.histFill;
  sparkline(g, 174, 48, 130, 42, s.vbatHist + (UI_HIST - n0), n0, 3300, 4250, k, true);
  g.text(174, 94, "3.3 V", T.dim, FONT_S);
  g.textR(304, 94, "4.2 V", T.dim, FONT_S);
  snprintf(b, sizeof(b), "USB  %.1f V", s.vbus);   g.text(174, 122, b, T.dim, FONT_M);
  snprintf(b, sizeof(b), "SYS  %.2f V", s.vsys);   g.text(174, 143, b, T.dim, FONT_M);

  bool sel = n.sel == 0;
  card(g, 8, 114, 150, 38, sel);
  icoMoon(g, 32, 133, sel ? T.accent : T.dim, sel ? T.cardHi : T.card);
  g.text(54, 130, "Sleep now", sel ? T.text : T.dim, FONT_B);
  g.text(54, 142, s.sleepMin ? "or after idle" : "auto-sleep off", T.dim, FONT_S);
  wipe2(g, s, n);
}

// ---------------------------------------------------------------- settings
static const char *kBright[5] = {"20%", "40%", "60%", "80%", "100%"};
static const char *kLed[4] = {"Off", "Low", "Medium", "High"};
static const char *kAccent[4] = {"Green", "Blue", "Purple", "Orange"};
static const char *kDim[4] = {"Never", "30 sec", "1 min", "5 min"};
static const char *kFx[3] = {"Amber", "Breathe", "Rainbow"};
static const char *kPairWin[4] = {"30 sec", "1 min", "2 min", "5 min"};

static const char *kLabels[SETTINGS_ROWS] = {
  "Brightness", "Auto-dim", "Auto-sleep", "Accent colour", "LED ring", "Ring idle effect", "Flip screen", "Knob direction",
  "USB mode", "Stick deadzone", "Xbox button wakes PC", "Pairing window", "Pair on power-up", "Reset settings",
  "Forget all controllers", "Bootloader mode", "About"};

static const char *kHints[SETTINGS_ROWS] = {
  "screen backlight", "dim the screen when idle", "deep sleep after this long with no controller", "colour of the interface",
  "brightness of the 8 LEDs", "what the ring does with no controller", "rotate the screen 180 degrees", "reverse the turn direction",
  "Wired 360 pad: most games.  360 receiver: dongle", "ignore small stick drift (applies on the PC)",
  "Xbox button wakes a sleeping computer", "how long pairing stays open", "open pairing every time it powers on",
  "back to factory settings", "unpair every controller", "reboot into the USB flasher", "version, memory, reset info"};

const char *settingsHint(int row) { return row >= 0 && row < SETTINGS_ROWS ? kHints[row] : ""; }
int settingsDanger(int row) { return row == 13 || row == 14 || row == 15; }

void settingsValue(const Snap &s, int row, char *out, size_t n) {
  switch (row) {
    case 0: snprintf(out, n, "%s", kBright[s.brightness % 5]); break;
    case 1: snprintf(out, n, "%s", kDim[s.autoDim % 4]); break;
    case 2: if (s.sleepMin) snprintf(out, n, "%u min", s.sleepMin); else snprintf(out, n, "Off"); break;
    case 3: snprintf(out, n, "%s", kAccent[s.accent % 4]); break;
    case 4: snprintf(out, n, "%s", kLed[s.ledLevel % 4]); break;
    case 5: snprintf(out, n, "%s", kFx[s.ringFx % 3]); break;
    case 6: snprintf(out, n, "%s", s.flip ? "Flipped" : "Normal"); break;
    case 7: snprintf(out, n, "%s", s.reverseKnob ? "Reversed" : "Normal"); break;
    case 8: snprintf(out, n, "%s", usbModeName(s.style)); break;
    case 9: if (s.deadzone) snprintf(out, n, "%u%%", s.deadzone * 5); else snprintf(out, n, "None"); break;
    case 10: snprintf(out, n, "%s", s.wakePc ? "On" : "Off"); break;
    case 11: snprintf(out, n, "%s", kPairWin[s.pairWin % 4]); break;
    case 12: snprintf(out, n, "%s", s.pairBoot ? "On" : "Off"); break;
    default: out[0] = 0; break;
  }
}

void scr_settings(G &g, const Snap &s, const Nav &n) {
  g.rect(0, 0, 320, 170, T.bg);
  int off = (int)lroundf(n.scrollAnim * 24);
  uint32_t dt = s.nowMs - n.screenSinceMs;
  for (int r = 0; r < SETTINGS_ROWS; r++) {
    int y = SB_H + 5 + r * 24 - off;
    if (y < SB_H - 20 || y > FOOT_Y - 6) continue;
    bool sel = n.sel == r;
    bool danger = settingsDanger(r);
    int dx = 0;                                   // rows glide in from the right when the screen opens
    int rowDelay = (r > 6 ? 6 : r) * 30;
    if (dt < 300 + (unsigned)rowDelay) { int d = (int)dt - rowDelay; if (d < 0) d = 0; float e = d / 300.0f; if (e > 1) e = 1; float q = 1 - e; dx = (int)(q * q * q * 120); }
    card(g, 8 + dx, y, 304, 21, sel, 0);
    if (sel) g.rrect(8 + dx, y, 5, 21, 2, danger ? T.bad : T.accent);
    g.text(22 + dx, y + 15, kLabels[r], sel ? T.text : T.dim, FONT_M);
    char v[20];
    settingsValue(s, r, v, sizeof(v));
    if (r == 3) g.disc(294 + dx, y + 10, 6, s.accent == 0 ? rgb(34, 214, 92) : s.accent == 1 ? rgb(72, 152, 255) : s.accent == 2 ? rgb(168, 112, 255) : rgb(255, 148, 40));
    else if (v[0]) g.textR(302 + dx, y + 15, v, sel ? T.accent : T.dim, FONT_B);
    else if (sel && danger) g.textR(302 + dx, y + 15, n.confirm ? "click again!" : "click", T.bad, FONT_B);
    else if (r == 16) g.textR(302 + dx, y + 15, ">", T.dim, FONT_B);
  }
  ui_chrome(g, s, n, "Settings", settingsHint(n.sel), true);
  // scroll bar
  int th = 120 * 5 / SETTINGS_ROWS, ty = SB_H + 5 + (120 - th) * n.sel / (SETTINGS_ROWS - 1);
  g.rrect(315, SB_H + 5, 2, 120, 1, T.border);
  g.rrect(315, ty, 2, th, 1, T.accent);
  wipe2(g, s, n);
}

// ---------------------------------------------------------------- about
void scr_about(G &g, const Snap &s, const Nav &n) {
  ui_chrome(g, s, n, "About", "key: back");
  card(g, 8, 28, 304, 124, false);
  icoInfo(g, 38, 58, T.accent, T.card);
  g.text(66, 52, "Xbox Bridge", T.text, FONT_L);
  char b[48];
  if (s.resetReason) snprintf(b, sizeof(b), "fw %s  reset: %s L%u U%u", s.fwVersion ? s.fwVersion : "dev", s.resetReason, s.prevMark, s.prevUiMark);
  else snprintf(b, sizeof(b), "firmware %s", s.fwVersion ? s.fwVersion : "dev");
  g.text(66, 66, b, T.dim, FONT_S);
  if (s.resetReason) snprintf(b, sizeof(b), "slept: %s   pmic fault: %02x", s.sleepWhy ? s.sleepWhy : "-", s.pmicFault);
  else snprintf(b, sizeof(b), "%02x:%02x:%02x:%02x:%02x:%02x", s.mac[0], s.mac[1], s.mac[2], s.mac[3], s.mac[4], s.mac[5]);
  g.text(66, 78, b, T.dim, FONT_S);
  unsigned up = s.uptimeS;
  if (up >= 3600) snprintf(b, sizeof(b), "%uh %02um", up / 3600, up / 60 % 60);
  else snprintf(b, sizeof(b), "%um %02us", up / 60, up % 60);
  stat(g, 20, 92, "UPTIME", b, T.text);
  snprintf(b, sizeof(b), "%luk / %luk", (unsigned long)(s.perf.heapFree / 1024), (unsigned long)(s.perf.heapMin / 1024));
  stat(g, 170, 92, "MEMORY free / low", b, T.text);
  snprintf(b, sizeof(b), "%d  %s", s.gamepads, usbModeName(s.style));
  stat(g, 20, 122, "XBOX PADS ON USB", b, T.text);
  snprintf(b, sizeof(b), "%d paired", s.nKnown);
  stat(g, 170, 122, "CONTROLLERS", b, T.text);
  wipe2(g, s, n);
}
