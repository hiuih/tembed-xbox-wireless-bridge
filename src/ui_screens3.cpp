// Latency and event-log screens.
#include "ui_screens.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static float grow(const Snap &s, const Nav &n, int delayMs) {
  int d = (int)(s.nowMs - n.screenSinceMs) - delayMs;
  if (d <= 0) return 0;
  float e = d / 420.0f;
  if (e > 1) e = 1;
  float q = 1 - e;
  return 1 - q * q * q;   // ease-out
}

// ---------------------------------------------------------------- latency
void scr_lag(G &g, const Snap &s, const Nav &n) {
  int online[MAX_PADS], cnt = 0;
  for (int i = 0; i < MAX_PADS; i++) if (s.pad[i].present && s.pad[i].online) online[cnt++] = i;
  char b[40];
  if (!cnt) {
    ui_chrome(g, s, n, "Latency", "key: back");
    g.textC(160, 84, "no controller connected", T.dim, FONT_B);
    g.textC(160, 102, "connect one to see its timing", T.dim, FONT_M);
    return;
  }
  int k = n.sel % cnt; if (k < 0) k = 0;
  const PadView &p = s.pad[online[k]];
  snprintf(b, sizeof(b), "Latency  -  Pad %d", online[k] + 1);
  ui_chrome(g, s, n, b, cnt > 1 ? "turn: next controller   key: back" : "key: back");

  // left: histogram of the time between input reports
  card(g, 8, 28, 204, 124, false);
  g.text(16, 38, "TIME BETWEEN INPUTS", T.dim, FONT_S);
  uint32_t tot = 0, mx = 1;
  for (int i = 0; i < 6; i++) { tot += p.gap[i]; if (p.gap[i] > mx) mx = p.gap[i]; }
  static const char *lab[6] = {"<10", "<20", "<40", "<80", "<160", "160+"};
  col_t cl[6] = {T.good, T.good, T.warn, T.warn, T.bad, T.bad};
  for (int i = 0; i < 6; i++) {
    int x = 18 + i * 31;
    float f = tot ? (float)p.gap[i] / mx : 0;
    int h = (int)(f * 62 * grow(s, n, i * 50));
    g.rrect(x, 120 - h, 22, h + 1, 3, mix(T.card, cl[i], 90));
    g.rrect(x, 120 - h, 22, h > 3 ? 3 : h + 1, 1, cl[i]);
    unsigned pc = tot ? (unsigned)(p.gap[i] * 100ULL / tot) : 0;
    snprintf(b, sizeof(b), "%u%%", pc);
    { int ly = 120 - h - 11; if (ly < 48) ly = 48; g.textC(x + 11, ly, b, pc ? T.text : T.dim, FONT_S); }
    g.textC(x + 11, 135, lab[i], T.dim, FONT_S);
  }
  g.hline(14, 124, 192, T.border);
  g.textR(206, 38, "ms", T.dim, FONT_S);

  // right: numbers
  card(g, 218, 28, 94, 124, false);
  snprintf(b, sizeof(b), "%u", p.rateHz);
  g.text(228, 56, b, T.accent, FONT_XL);
  g.text(228, 66, "reports / s", T.dim, FONT_S);
  int sidx = k > 1 ? 1 : k;
  int n0 = s.histFill < 2 ? 2 : s.histFill;
  sparkline(g, 226, 72, 78, 22, s.rateHist[sidx] + (UI_HIST - n0), n0, 0, 140, T.accent, true);
  snprintf(b, sizeof(b), "loss %u.%u%%", p.lossPermille / 10, p.lossPermille % 10);
  g.text(228, 112, b, p.lossPermille > 150 ? T.bad : p.lossPermille > 60 ? T.warn : T.good, FONT_M);
  snprintf(b, sizeof(b), "%d dBm", p.rssi);
  g.text(228, 129, b, T.dim, FONT_M);
  snprintf(b, sizeof(b), "loop %lu us", (unsigned long)s.perf.loopMaxUs);
  g.text(228, 140, b, T.dim, FONT_S);
}

// ---------------------------------------------------------------- event log
void scr_log(G &g, const Snap &s, const Nav &n) {
  ui_chrome(g, s, n, "Event log", "turn: scroll   key: back");
  card(g, 8, 28, 304, 124, false);
  if (!s.logN) { g.textC(160, 94, "nothing has happened yet", T.dim, FONT_M); return; }
  int rows = 7;
  int top = n.sel >= rows ? n.sel - rows + 1 : 0;
  uint32_t dt = s.nowMs - n.screenSinceMs;
  for (int r = 0; r < rows; r++) {
    int i = top + r;
    if (i >= s.logN) break;
    int y = 31 + r * 17;
    int dx = 0;
    unsigned delay = r * 32;
    if (dt < 300 + delay) { int d = (int)dt - (int)delay; if (d < 0) d = 0; float q = 1 - d / 300.0f; dx = (int)(q * q * 90); }
    bool sel = i == n.sel;
    if (sel) g.rrect(12 + dx, y, 290, 16, 3, T.cardHi);
    const char *m = s.logMsg[i];
    col_t c = T.text;
    if (m[0] == '+') c = T.good; else if (m[0] == '-') c = T.bad; else if (m[0] == '!') c = T.warn;
    if (m[0] == '+' || m[0] == '-' || m[0] == '!') m++;
    g.disc(20 + dx, y + 8, 3, c);
    char t[12];
    unsigned at = s.logAtS[i];
    snprintf(t, sizeof(t), "%02u:%02u", (at / 60) % 100, at % 60);
    g.text(30 + dx, y + 4, t, T.dim, FONT_S);
    g.text(72 + dx, y + 13, m, sel ? T.text : mix(T.dim, T.text, 170), FONT_M);
  }
  if (s.logN > rows) {   // scroll bar
    int th = 108 * rows / s.logN; if (th < 8) th = 8;
    int ty = 34 + (108 - th) * n.sel / (s.logN - 1);
    g.rrect(305, 34, 2, 108, 1, T.border);
    g.rrect(305, ty, 2, th, 1, T.accent);
  }
}
