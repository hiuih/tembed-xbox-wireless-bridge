// Update mode screens: menu, Wi-Fi scan list, password keyboard, WPS, progress.
#include "ui_screens.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static const char *kPages[3] = {"abcdefghijklmnopqrstuvwxyz0123456789", "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789", "!@#$%^&*()-_=+[]{};:',.<>/?|~`\\\""};

static void spinner(G &g, int cx, int cy, int r, uint32_t t, col_t k) {
  int a = (int)((t / 3) % 360);
  g.ring(cx, cy, r, T.border);
  arc(g, cx, cy, r, 4, a, a + 110, k);
  arc(g, cx, cy, r, 4, a + 180, a + 215, mix(T.bg, k, 120));
}

static void menuRow(G &g, int y, bool sel, const char *label, const char *val) {
  card(g, 8, y, 304, 22, sel, 0);
  if (sel) g.rrect(8, y, 5, 22, 2, T.accent);
  g.text(22, y + 16, label, sel ? T.text : T.dim, FONT_M);
  if (val && val[0]) g.textR(302, y + 16, val, sel ? T.accent : T.dim, FONT_B);
}

static void bigText(G &g, int y, const char *s, col_t k) { g.textC(160, y, s, k, FONT_B); }

void scr_update(G &g, const Snap &s, const Nav &n) {
  const UpdView &u = s.upd;
  g.rect(0, 0, 320, 170, T.bg);
  uint32_t t = s.nowMs;
  char b[72];
  switch (u.state) {
    case U_MENU: {
      ui_chrome(g, s, n, "Update", "turn: move   click: choose   key: exit", true);
      card(g, 8, 28, 304, 44, false);
      icoDown(g, 34, 50, T.accent, T.card);
      snprintf(b, sizeof(b), "Installed  %s", s.fwVersion ? s.fwVersion : "?");
      g.text(60, 46, b, T.text, FONT_B);
      if (u.saved[0]) snprintf(b, sizeof(b), "Wi-Fi: %s", u.saved); else snprintf(b, sizeof(b), "Wi-Fi: not set up yet");
      g.text(60, 52, b, T.dim, FONT_S);
      if (u.detail[0]) { snprintf(b, sizeof(b), "%s  %s", u.msg, u.detail); g.text(60, 62, b, T.good, FONT_S); }
      menuRow(g, 80, n.sel == 0, "Check for updates", "");
      menuRow(g, 104, n.sel == 1, "Wi-Fi network", u.saved[0] ? u.saved : "set up");
      menuRow(g, 128, n.sel == 2, "Exit update mode", "");
      break;
    }
    case U_SCAN: {
      ui_chrome(g, s, n, "Choose a network", "turn: move   click: pick   key: back", true);
      int rows = u.nNets + 3;
      int top = n.sel > 4 ? n.sel - 4 : 0;
      for (int r = 0; r < 5; r++) {
        int i = top + r;
        if (i >= rows) break;
        int y = 28 + r * 25;
        bool sel = n.sel == i;
        card(g, 8, y, 296, 22, sel, 0);
        if (sel) g.rrect(8, y, 5, 22, 2, T.accent);
        col_t tc = sel ? T.text : T.dim;
        if (i == 0) { g.text(22, y + 16, "Use the router's WPS button", tc, FONT_M); }
        else if (i <= u.nNets) {
          g.text(22, y + 16, u.ssid[i - 1], tc, FONT_M);
          signalBars(g, 282, y + 5, u.rssi[i - 1]);
          if (u.secure[i - 1]) { g.rrect(262, y + 10, 9, 7, 1, tc); g.ring(266, y + 9, 3, tc); }
        }
        else if (i == u.nNets + 1) g.text(22, y + 16, "Scan again", tc, FONT_M);
        else g.text(22, y + 16, "Back", tc, FONT_M);
      }
      if (rows > 5) { int th = 120 * 5 / rows, ty = 28 + (120 - th) * n.sel / (rows - 1); g.rrect(310, 28, 2, 120, 1, T.border); g.rrect(310, ty, 2, th, 1, T.accent); }
      break;
    }
    case U_PASS: {
      snprintf(b, sizeof(b), "Password for %s", u.detail);
      ui_chrome(g, s, n, "Wi-Fi password", "turn: move   click: type   key: delete", true);
      card(g, 8, 26, 304, 22, false);
      int len = (int)strlen(s.kb);
      char shown[70];
      if (s.kbShow) snprintf(shown, sizeof(shown), "%s", s.kb);
      else { for (int i = 0; i < len && i < 66; i++) shown[i] = (i == len - 1 && (t / 400) % 2 == 0) ? s.kb[i] : '*'; shown[len < 66 ? len : 66] = 0; }
      if (len == 0) g.text(16, 42, b, T.dim, FONT_S);
      else { int w = g.textW(shown, FONT_M); g.text(w > 284 ? 300 - w : 16, 42, shown, T.text, FONT_M); }
      if ((t / 500) % 2 == 0) g.rect(len == 0 ? 16 : (g.textW(shown, FONT_M) > 284 ? 302 : 18 + g.textW(shown, FONT_M)), 31, 2, 12, T.accent);
      const char *chars = kPages[s.kbPage % 3];
      int nch = (int)strlen(chars), cnt = nch + 5;
      static const char *sp[5] = {"Aa", "DEL", "SPC", "SHOW", "OK"};
      for (int i = 0; i < cnt; i++) {
        int col = i % 10, row = i / 10;
        int x = 8 + col * 30 + col / 1 * 0, y = 52 + row * 20;
        bool sel = s.kbIdx == i;
        bool special = i >= nch;
        col_t fill = sel ? mix(T.accentDim, T.accent, 140 + (int)(80 * sinf(t / 130.0f))) : (special ? T.cardHi : T.card);
        g.rrect(x + 1, y, 28, 18, 4, fill);
        char one[2] = {0, 0};
        const char *lab;
        if (special) {
          int sidx = i - nch;
          lab = sidx == 3 ? (s.kbShow ? "HIDE" : "SHOW") : sidx == 0 ? (s.kbPage == 0 ? "ABC" : s.kbPage == 1 ? "#+=" : "abc") : sp[sidx];
          g.textC(x + 15, y + 5, lab, sel ? T.bg : (sidx == 4 ? T.accent : T.dim), FONT_S);
        } else { one[0] = chars[i]; g.textC(x + 15, y + 14, one, sel ? T.bg : T.text, FONT_B); }
      }
      break;
    }
    case U_WPS: {
      ui_chrome(g, s, n, "WPS", "click or key: cancel", true);
      for (int i = 0; i < 3; i++) { int ph = (t / 12 + i * 85) % 255; g.ring(160, 80, 12 + ph / 5, mix(T.bg, T.accent, 255 - ph)); }
      g.disc(160, 80, 14, T.accentDim);
      icoPair(g, 160, 80, T.accent, T.accentDim);
      bigText(g, 128, "Press the WPS button on your router", T.text);
      snprintf(b, sizeof(b), "%us left", (unsigned)u.wpsLeftS);
      g.textC(160, 146, b, T.dim, FONT_M);
      break;
    }
    case U_BUSY: {
      ui_chrome(g, s, n, "Update", "click or key: cancel", true);
      spinner(g, 160, 76, 26, t, T.accent);
      bigText(g, 128, u.msg, T.text);
      if (u.detail[0]) g.textC(160, 144, u.detail, T.dim, FONT_M);
      break;
    }
    case U_READY: {
      ui_chrome(g, s, n, "Update available", "turn: move   click: choose", true);
      card(g, 8, 30, 304, 52, false);
      icoDown(g, 36, 56, T.accent, T.card);
      g.text(66, 52, u.detail, T.text, FONT_L);
      snprintf(b, sizeof(b), "%u KB, then Launcher installs it", (unsigned)(u.size / 1024));
      g.text(66, 66, b, T.dim, FONT_S);
      menuRow(g, 92, n.sel == 0, "Download and install", "");
      menuRow(g, 118, n.sel == 1, "Not now", "");
      break;
    }
    case U_UPTODATE: {
      ui_chrome(g, s, n, "Update", "click or key: back", true);
      g.disc(160, 74, 28, T.accentDim); g.ring(160, 74, 28, T.accent);
      g.line(146, 74, 156, 84, T.accent); g.line(146, 75, 156, 85, T.accent); g.line(156, 84, 176, 62, T.accent); g.line(156, 85, 176, 63, T.accent);
      bigText(g, 128, "You are up to date", T.text);
      g.textC(160, 144, u.detail, T.dim, FONT_M);
      break;
    }
    case U_DOWNLOAD: {
      ui_chrome(g, s, n, "Downloading", "click or key: cancel", true);
      icoDown(g, 160, 54 + (int)(3 * sinf(t / 160.0f)), T.accent, T.bg);
      int w = 260, x = 30, y = 92;
      g.rrect(x, y, w, 14, 7, T.border);
      int fw = u.progress * w / 100;
      if (fw > 2) { g.rrect(x, y, fw, 14, 7, T.accent); int sh = (int)((t / 6) % (fw + 40)) - 20; for (int i = 0; i < 18; i++) { int px = x + sh + i; if (px >= x + 4 && px < x + fw - 4) g.vline(px, y + 2, 10, mix(T.accent, T.text, 80 + i * 8)); } }
      snprintf(b, sizeof(b), "%u%%", u.progress);
      g.textC(160, 128, b, T.text, FONT_L);
      g.textC(160, 146, u.msg, T.dim, FONT_S);
      break;
    }
    case U_DONE: {
      ui_chrome(g, s, n, "Downloaded", "", true);
      g.disc(160, 62, 26, T.accentDim); g.ring(160, 62, 26, T.accent);
      g.line(148, 62, 157, 71, T.accent); g.line(148, 63, 157, 72, T.accent); g.line(157, 71, 174, 52, T.accent); g.line(157, 72, 174, 53, T.accent);
      bigText(g, 112, "Saved to the SD card", T.text);
      snprintf(b, sizeof(b), "Restarting into Launcher in %us", (unsigned)u.doneLeftS);
      g.textC(160, 128, b, T.dim, FONT_S);
      g.textC(160, 141, "press the knob > SD > XboxBridge to install", T.dim, FONT_S);
      break;
    }
    case U_ERROR: {
      ui_chrome(g, s, n, "Update", "click or key: back", true);
      g.disc(160, 66, 26, mix(T.bg, T.bad, 70)); g.ring(160, 66, 26, T.bad);
      g.rrect(158, 52, 5, 18, 2, T.bad); g.disc(160, 76, 3, T.bad);
      bigText(g, 118, u.msg, T.text);
      g.textC(160, 138, u.detail, T.dim, FONT_M);
      break;
    }
  }
}
