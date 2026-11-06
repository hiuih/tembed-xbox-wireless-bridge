#include "ui_screens.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

const char *ui_screen_title(Screen sc) {
  static const char *t[SC_COUNT] = {"Xbox Bridge", "Xbox Bridge", "Controllers", "Controller", "Pair", "Radio", "Spectrum", "Power", "Settings", "About"};
  return t[sc];
}

// ---------------------------------------------------------------- chrome
void ui_chrome(G &g, const Snap &s, const Nav &n, const char *title, const char *hint, bool overlay) {
  if (!overlay) g.rect(0, 0, 320, 170, T.bg);
  vgrad(g, 0, 0, 320, SB_H, mix(T.bg, T.accentDim, 230), T.bg);
  g.hline(0, SB_H, 320, mix(T.bg, T.accent, 70));
  g.rrect(8, 6, 4, 11, 2, T.accent);
  g.text(18, 17, title, T.text, FONT_B);

  char b[16];
  batteryIcon(g, 288, 5, s.battPct, s.charging, s.pmicOk);
  if (s.pmicOk) { snprintf(b, sizeof(b), "%d%%", s.battPct); g.textR(284, 7, b, T.dim, FONT_S); }
  col_t uk = !s.usbMounted ? T.dim : s.usbSuspended ? T.warn : T.good;
  usbIcon(g, 212, 5, uk);
  if (s.usbMounted) { snprintf(b, sizeof(b), "%d", s.gamepads); g.text(233, 7, b, uk, FONT_S); }
  if (s.pairing) {
    uint32_t ph = (s.nowMs / 6) & 255;
    int pulse = ph < 128 ? ph * 2 : (255 - ph) * 2;
    snprintf(b, sizeof(b), "PAIRING %us", (unsigned)(s.pairLeftMs / 1000));
    pill(g, 96, 4, 100, 15, mix(T.blue, T.accent, pulse), b, T.bg, FONT_S);
  }
  ui_footer(g, hint, overlay);
}

void ui_footer(G &g, const char *hint, bool clear) {
  if (clear) g.rect(0, FOOT_Y - 2, 320, 16, T.bg);
  g.hline(0, FOOT_Y - 2, 320, T.border);
  if (hint) g.textC(160, FOOT_Y + 3, hint, T.dim, FONT_S);
}

void ui_draw_toast(G &g, const Nav &n, uint32_t now) {
  if ((int32_t)(n.toastUntilMs - now) <= 0) return;
  int w = g.textW(n.toast, FONT_B) + 28;
  g.rrect(160 - w / 2, 116, w, 26, 13, T.accent);
  g.textC(160, 134, n.toast, T.bg, FONT_B);
}

static void wipe(G &g, const Snap &s, const Nav &n) {
  uint32_t dt = s.nowMs - n.screenSinceMs;
  if (dt > 260) return;
  int x = dt * 340 / 260 - 10;
  for (int i = 0; i < 6; i++) g.rect(x - i * 6, SB_H + 1, 3, FOOT_Y - SB_H - 4, mix(T.bg, T.accent, 200 - i * 36));
}

// ---------------------------------------------------------------- splash
void scr_splash(G &g, const Snap &s, const Nav &n) {
  g.rect(0, 0, 320, 170, T.bg);
  uint32_t t = s.nowMs - n.screenSinceMs;
  float e = t > 1200 ? 1.0f : t / 1200.0f;
  int cy = 66 + (int)((1.0f - e) * 12);
  // glow rings
  for (int i = 3; i >= 0; i--) g.ring(160, cy, 34 + i * 7, mix(T.bg, T.accent, (uint8_t)(e * (80 - i * 18))));
  g.disc(160, cy, 30, T.accentDim);
  icoGamepad(g, 160, cy, mix(T.bg, T.accent, (uint8_t)(100 + e * 155)));
  g.textC(160, 128, "BRIDGE", mix(T.bg, T.text, (uint8_t)(e * 255)), FONT_XL);
  g.textC(160, 146, "xbox wireless  >  usb", mix(T.bg, T.dim, (uint8_t)(e * 255)), FONT_M);
  bar(g, 100, 158, 120, 4, e, T.accent, T.border);
}

// ---------------------------------------------------------------- home
struct Tile { const char *name; Screen sc; };
static const Tile kTiles[6] = {
  {"Controllers", SC_PADS}, {"Pair", SC_PAIR}, {"Radio", SC_RADIO}, {"Spectrum", SC_SPECTRUM}, {"Power", SC_POWER}, {"Settings", SC_SETTINGS}};

static void tileIcon(G &g, int i, int cx, int cy, col_t k, col_t hole) {
  switch (i) {
    case 0: icoGamepad(g, cx, cy, k, hole); break;
    case 1: icoPair(g, cx, cy, k, hole); break;
    case 2: icoRadio(g, cx, cy, k, hole); break;
    case 3: icoSpectrum(g, cx, cy, k, hole); break;
    case 4: icoPower(g, cx, cy, k, hole); break;
    default: icoGear(g, cx, cy, k, hole); break;
  }
}

void scr_home(G &g, const Snap &s, const Nav &n) {
  ui_chrome(g, s, n, "Xbox Bridge", "turn: move    click: open    hold: pair    key: sleep");
  char sub[24];
  for (int i = 0; i < 6; i++) {
    int x = 8 + (i % 3) * 104, y = 28 + (i / 3) * 64;
    bool sel = (n.sel == i);
    card(g, x, y, 96, 58, sel);
    tileIcon(g, i, x + 48, y + 16, sel ? T.accent : mix(T.dim, T.text, 90), sel ? T.cardHi : T.card);
    g.textC(x + 48, y + 43, kTiles[i].name, sel ? T.text : mix(T.dim, T.text, 150), FONT_M);
    switch (i) {
      case 0: snprintf(sub, sizeof(sub), "%d online", s.nReady); break;
      case 1: if (s.pairing) snprintf(sub, sizeof(sub), "open %us", (unsigned)(s.pairLeftMs / 1000)); else snprintf(sub, sizeof(sub), "%d paired", s.nKnown); break;
      case 2: snprintf(sub, sizeof(sub), "ch %u  %lu drop", s.channel, (unsigned long)s.perf.queueDrops); break;
      case 3: snprintf(sub, sizeof(sub), "%s", s.nrfOk ? "nRF24 ready" : "no nRF24"); break;
      case 4: if (s.pmicOk) snprintf(sub, sizeof(sub), "%d%% %s", s.battPct, s.charging ? "charging" : "battery"); else snprintf(sub, sizeof(sub), "no gauge"); break;
      default: snprintf(sub, sizeof(sub), "USB style %c", 'A' + s.style); break;
    }
    g.textC(x + 48, y + 49, sub, T.dim, FONT_S);
  }
  // animated selector outline slides between tiles
  int sx = (int)n.selX, sy = (int)n.selY;
  g.rframe(sx - 2, sy - 2, 100, 62, 9, mix(T.bg, T.accent, 90));
  wipe(g, s, n);
}

// ---------------------------------------------------------------- controller list
static void padSub(const PadView &p, char *b, size_t n) {
  if (p.online) snprintf(b, n, "ready   %u Hz   loss %u.%u%%", p.rateHz, p.lossPermille / 10, p.lossPermille % 10);
  else if (p.connecting) snprintf(b, n, "connecting...");
  else snprintf(b, n, "paired - press the Xbox button");
}

void scr_pads(G &g, const Snap &s, const Nav &n) {
  g.rect(0, 0, 320, 170, T.bg);
  int rows = 0, idx[MAX_PADS + 1];
  for (int i = 0; i < MAX_PADS; i++) if (s.pad[i].present) idx[rows++] = i;
  idx[rows] = -1;       // last row = "pair new"
  int total = rows + 1;
  int off = (int)lroundf(n.scrollAnim * 42);
  for (int r = 0; r < total; r++) {
    int y = SB_H + 6 + r * 42 - off;
    if (y < SB_H - 36 || y > FOOT_Y) continue;
    bool sel = (n.sel == r);
    if (idx[r] < 0) {
      card(g, 8, y, 304, 38, sel);
      icoPair(g, 34, y + 19, sel ? T.accent : T.dim);
      g.text(60, y + 17, "Pair a new controller", sel ? T.text : T.dim, FONT_B);
      g.text(60, y + 27, s.pairing ? "pairing is open" : "click to open pairing", T.dim, FONT_S);
      continue;
    }
    const PadView &p = s.pad[idx[r]];
    card(g, 8, y, 304, 38, sel);
    g.disc(34, y + 19, 14, p.online ? T.accentDim : T.border);
    g.ring(34, y + 19, 14, p.online ? T.accent : T.dim);
    char b[40];
    snprintf(b, sizeof(b), "%d", idx[r] + 1);
    g.textC(34, y + 25, b, p.online ? T.accent : T.dim, FONT_B);
    g.text(60, y + 17, p.name, p.online ? T.text : T.dim, FONT_B);
    padSub(p, b, sizeof(b));
    g.text(60, y + 27, b, T.dim, FONT_S);
    if (p.online) {
      batteryIcon(g, 256, y + 5, p.battLevel * 33 + 10, false, p.battKnown);
      signalBars(g, 288, y + 5, p.rssi);
      char r2[12]; snprintf(r2, sizeof(r2), "%d dBm", p.rssi);
      g.textR(304, y + 27, r2, T.dim, FONT_S);
    }
  }
  ui_chrome(g, s, n, "Controllers", "turn: move   click: open   key: back", true);
  wipe(g, s, n);
}

// ---------------------------------------------------------------- live controller view
static void stick(G &g, int cx, int cy, int r, int16_t x, int16_t y, bool pressed, const PadView &p, int which) {
  g.disc(cx, cy, r, T.card);
  g.ring(cx, cy, r, pressed ? T.accent : T.border);
  g.ring(cx, cy, r / 2, mix(T.card, T.border, 160));
  g.hline(cx - r + 3, cy, 2 * r - 6, mix(T.card, T.border, 120));
  g.vline(cx, cy - r + 3, 2 * r - 6, mix(T.card, T.border, 120));
  int rr = r - 5;
  for (int a = 7; a >= 1; a--) {   // fading trail
    int tx = cx + p.trail[a][which * 2] * rr / 100, ty = cy + p.trail[a][which * 2 + 1] * rr / 100;
    g.disc(tx, ty, 2, mix(T.card, T.accent, 160 - a * 20));
  }
  int dx = cx + (int)((int32_t)x * rr / 32768), dy = cy + (int)((int32_t)y * rr / 32768);
  g.disc(dx, dy, 5, pressed ? T.text : T.accent);
  g.ring(dx, dy, 5, T.bg);
}

static void dpadCell(G &g, int x, int y, int w, int h, bool on) {
  g.rrect(x, y, w, h, 2, on ? T.accent : T.border);
}

void scr_pad(G &g, const Snap &s, const Nav &n) {
  const PadView &p = s.pad[n.padSlot < 0 ? 0 : n.padSlot];
  char b[40];
  snprintf(b, sizeof(b), "Controller %d", n.padSlot + 1);
  ui_chrome(g, s, n, b, "turn: choose   click: do   key: back");
  const PadInput &in = p.in;
  if (!p.online) {
    g.textC(160, 80, p.connecting ? "connecting..." : "controller is offline", T.dim, FONT_B);
    g.textC(160, 98, "press its Xbox button", T.dim, FONT_M);
  } else {
    // triggers + bumpers
    g.text(10, 31, "LT", T.dim, FONT_S);
    bar(g, 26, 30, 96, 9, in.lt / 1023.0f, T.accent, T.border);
    g.text(298, 31, "RT", T.dim, FONT_S);
    bar(g, 198, 30, 96, 9, in.rt / 1023.0f, T.accent, T.border);
    pill(g, 10, 43, 40, 13, (in.buttons & GIP_BTN_LB) ? T.accent : T.border, "LB", (in.buttons & GIP_BTN_LB) ? T.bg : T.dim, FONT_S);
    pill(g, 270, 43, 40, 13, (in.buttons & GIP_BTN_RB) ? T.accent : T.border, "RB", (in.buttons & GIP_BTN_RB) ? T.bg : T.dim, FONT_S);
    // sticks, d-pad, face buttons
    stick(g, 58, 90, 23, in.lx, in.ly, in.buttons & GIP_BTN_L3, p, 0);
    stick(g, 206, 112, 23, in.rx, in.ry, in.buttons & GIP_BTN_R3, p, 1);
    int dx = 118, dy = 112;
    dpadCell(g, dx - 5, dy - 17, 10, 11, in.buttons & GIP_BTN_DUP);
    dpadCell(g, dx - 5, dy + 6, 10, 11, in.buttons & GIP_BTN_DDOWN);
    dpadCell(g, dx - 17, dy - 5, 11, 10, in.buttons & GIP_BTN_DLEFT);
    dpadCell(g, dx + 6, dy - 5, 11, 10, in.buttons & GIP_BTN_DRIGHT);
    g.rect(dx - 5, dy - 5, 10, 10, T.border);
    struct { int x, y; uint16_t m; col_t c; const char *l; } fb[4] = {
      {264, 74, GIP_BTN_Y, rgb(255, 200, 40), "Y"}, {282, 91, GIP_BTN_B, rgb(255, 80, 80), "B"},
      {264, 108, GIP_BTN_A, rgb(60, 214, 100), "A"}, {246, 91, GIP_BTN_X, rgb(80, 150, 255), "X"}};
    for (auto &f : fb) {
      bool on = in.buttons & f.m;
      g.disc(f.x, f.y, 9, on ? f.c : T.card);
      g.ring(f.x, f.y, 9, f.c);
      g.textC(f.x, f.y + 5, f.l, on ? T.bg : f.c, FONT_B);
    }
    g.disc(146, 76, 5, (in.buttons & GIP_BTN_VIEW) ? T.accent : T.border);
    g.disc(170, 76, 5, (in.buttons & GIP_BTN_MENU) ? T.accent : T.border);
    g.disc(158, 98, 9, in.guide ? T.accent : T.card);
    g.ring(158, 98, 9, in.guide ? T.accent : T.border);
    // info, top centre
    batteryIcon(g, 126, 31, p.battLevel * 33 + 10, false, p.battKnown);
    signalBars(g, 164, 30, p.rssi);
    snprintf(b, sizeof(b), "%u Hz", p.rateHz);
    g.textC(160, 51, b, T.dim, FONT_S);
  }
  // action chips
  static const char *acts[4] = {"Rumble", "LED", "Drop", "Forget"};
  for (int i = 0; i < 4; i++) {
    bool sel = n.sel == i;
    col_t danger = i >= 2 ? T.bad : T.accent;
    pill(g, 8 + i * 77, 138, 71, 15, sel ? danger : T.card, acts[i], sel ? T.bg : T.dim, FONT_S);
  }
  wipe(g, s, n);
}

// ---------------------------------------------------------------- pairing
void scr_pair(G &g, const Snap &s, const Nav &n) {
  ui_chrome(g, s, n, "Pair", s.pairing ? "click: stop pairing    key: back" : "click: start pairing    key: back");
  int cx = 58, cy = 88;
  uint32_t t = s.nowMs;
  if (s.pairing) {
    for (int i = 0; i < 3; i++) {   // expanding radar rings
      int ph = (t / 14 + i * 85) % 255;
      int r = 16 + ph * 28 / 255;
      g.ring(cx, cy, r, mix(T.bg, T.accent, 255 - ph));
    }
    arc(g, cx, cy, 46, 5, 0, (int)(s.pairLeftMs / 60000.0f * 360), T.accent);
  } else {
    g.ring(cx, cy, 46, T.border);
  }
  g.disc(cx, cy, 25, T.card);
  icoGamepad(g, cx, cy - 8, s.pairing ? T.accent : T.dim);
  char b[40];
  if (s.pairing) { snprintf(b, sizeof(b), "%us", (unsigned)(s.pairLeftMs / 1000)); g.textC(cx, cy + 21, b, T.text, FONT_B); }
  static const char *steps[3] = {"Turn it on", "Hold PAIR button", "Wait for fast blink"};
  for (int i = 0; i < 3; i++) {
    int y = 44 + i * 32;
    g.disc(128, y + 8, 10, s.pairing ? T.accentDim : T.border);
    snprintf(b, sizeof(b), "%d", i + 1);
    g.textC(128, y + 13, b, s.pairing ? T.accent : T.dim, FONT_B);
    g.text(146, y + 14, steps[i], s.pairing ? T.text : T.dim, FONT_M);
  }
  snprintf(b, sizeof(b), "%d paired  -  %d online", s.nKnown, s.nReady);
  g.text(118, 140, b, T.dim, FONT_S);
  wipe(g, s, n);
}

// ---------------------------------------------------------------- dispatcher
void ui_render(G &g, const Snap &s, const Nav &n) {
  switch (n.screen) {
    case SC_SPLASH: scr_splash(g, s, n); break;
    case SC_HOME: scr_home(g, s, n); break;
    case SC_PADS: scr_pads(g, s, n); break;
    case SC_PAD: scr_pad(g, s, n); break;
    case SC_PAIR: scr_pair(g, s, n); break;
    case SC_RADIO: scr_radio(g, s, n); break;
    case SC_SPECTRUM: scr_spectrum(g, s, n); break;
    case SC_POWER: scr_power(g, s, n); break;
    case SC_SETTINGS: scr_settings(g, s, n); break;
    case SC_ABOUT: scr_about(g, s, n); break;
    default: break;
  }
  if (n.screen != SC_SPLASH) ui_draw_toast(g, n, s.nowMs);
}
