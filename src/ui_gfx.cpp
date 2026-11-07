#include "ui_gfx.h"
#include <math.h>
#include <string.h>

Theme T;

col_t mix(col_t a, col_t b, uint8_t t) {
  int ar = (a >> 11) & 31, ag = (a >> 5) & 63, ab = a & 31;
  int br = (b >> 11) & 31, bg = (b >> 5) & 63, bb = b & 31;
  int r = ar + (br - ar) * t / 255, g = ag + (bg - ag) * t / 255, bl = ab + (bb - ab) * t / 255;
  return (r << 11) | (g << 5) | bl;
}

void theme_set(uint8_t accentIndex) {
  T.bg = rgb(8, 12, 18);
  T.card = rgb(17, 25, 35);
  T.cardHi = rgb(24, 38, 52);
  T.border = rgb(38, 52, 68);
  T.text = rgb(232, 238, 244);
  T.dim = rgb(124, 138, 154);
  T.good = rgb(46, 214, 104);
  T.warn = rgb(255, 184, 48);
  T.bad = rgb(255, 86, 86);
  T.blue = rgb(72, 152, 255);
  T.purple = rgb(168, 112, 255);
  switch (accentIndex) {
    case 1: T.accent = rgb(72, 152, 255); break;
    case 2: T.accent = rgb(168, 112, 255); break;
    case 3: T.accent = rgb(255, 148, 40); break;
    default: T.accent = rgb(34, 214, 92); break;
  }
  T.accentDim = mix(T.bg, T.accent, 70);
}

void G::text(int x, int y, const char *s, col_t k, const GFXfont *f) {
  c->setFont(f);
  c->setTextSize(1);
  c->setTextWrap(false);
  c->setTextColor(k);
  c->setCursor(x, y - oy);
  c->print(s);
}

int G::textW(const char *s, const GFXfont *f) {
  if (!f) return (int)strlen(s) * 6;
  c->setFont(f);
  c->setTextSize(1);
  int16_t x1, y1; uint16_t w, h;
  c->getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  return w + x1;
}

void vgrad(G &g, int x, int y, int w, int h, col_t top, col_t bottom) {
  int bandH = g.c->height();
  for (int i = 0; i < h; i++) {
    int yy = y + i;
    if (yy < g.oy || yy >= g.oy + bandH) continue;
    g.rect(x, yy, w, 1, mix(top, bottom, h > 1 ? i * 255 / (h - 1) : 0));
  }
}

void hgrad(G &g, int x, int y, int w, int h, col_t left, col_t right) {
  for (int i = 0; i < w; i += 2) g.rect(x + i, y, 2, h, mix(left, right, w > 1 ? i * 255 / (w - 1) : 0));
}

void card(G &g, int x, int y, int w, int h, bool selected, col_t fill) {
  g.rrect(x, y, w, h, 7, fill ? fill : (selected ? T.cardHi : T.card));
  if (selected) {
    g.rframe(x, y, w, h, 7, T.accent);
    g.rframe(x + 1, y + 1, w - 2, h - 2, 6, mix(T.accent, T.cardHi, 110));
  } else {
    g.rframe(x, y, w, h, 7, T.border);
  }
}

void pill(G &g, int x, int y, int w, int h, col_t fill, const char *s, col_t txt, const GFXfont *f) {
  g.rrect(x, y, w, h, h / 2, fill);
  g.textC(x + w / 2, y + h / 2 + (f == FONT_S ? -4 : 4), s, txt, f);
}

void arc(G &g, int cx, int cy, int r, int thick, int a0, int a1, col_t k) {
  for (int a = a0; a <= a1; a += 3) {
    float t = a * 0.0174533f;
    g.disc(cx + (int)lroundf(r * sinf(t)), cy - (int)lroundf(r * cosf(t)), thick / 2, k);
  }
}

void bar(G &g, int x, int y, int w, int h, float frac, col_t fg, col_t bg) {
  if (frac < 0) frac = 0;
  if (frac > 1) frac = 1;
  g.rrect(x, y, w, h, h / 2, bg);
  int fw = (int)(w * frac);
  if (fw >= h) g.rrect(x, y, fw, h, h / 2, fg);
  else if (fw > 0) g.rect(x, y, fw, h, fg);
}

void batteryIcon(G &g, int x, int y, int pct, bool charging, bool known) {
  g.rframe(x, y, 24, 12, 3, T.dim);
  g.rect(x + 24, y + 4, 2, 4, T.dim);
  if (!known) { g.text(x + 8, y + 2, "?", T.dim, FONT_S); return; }
  col_t k = pct < 15 ? T.bad : pct < 35 ? T.warn : T.good;
  int w = (20 * pct) / 100;
  if (w > 0) g.rrect(x + 2, y + 2, w, 8, 1, k);
  if (charging) {
    g.tri(x + 13, y + 1, x + 8, y + 7, x + 12, y + 7, T.text);
    g.tri(x + 11, y + 11, x + 16, y + 5, x + 12, y + 5, T.text);
  }
}

void signalBars(G &g, int x, int y, int rssi) {
  int bars = rssi > -50 ? 4 : rssi > -60 ? 3 : rssi > -70 ? 2 : rssi > -80 ? 1 : 0;
  col_t k = bars >= 3 ? T.good : bars == 2 ? T.warn : T.bad;
  for (int i = 0; i < 4; i++) {
    int h = 3 + i * 3;
    if (i < bars) g.rect(x + i * 4, y + 12 - h, 3, h, k);
    else g.rect(x + i * 4, y + 12 - h, 3, h, T.border);
  }
}

void usbIcon(G &g, int x, int y, col_t k) {
  g.rrect(x, y + 2, 14, 8, 2, k);
  g.rect(x + 3, y + 4, 2, 2, T.bg);
  g.rect(x + 9, y + 4, 2, 2, T.bg);
  g.rect(x + 14, y + 5, 3, 2, k);
}

void sparkline(G &g, int x, int y, int w, int h, const uint16_t *v, int n, uint16_t vmin, uint16_t vmax, col_t k, bool fill) {
  if (n < 2 || vmax <= vmin) return;
  int px = 0, py = 0;
  for (int i = 0; i < n; i++) {
    int vv = v[i] < vmin ? vmin : v[i] > vmax ? vmax : v[i];
    int xx = x + i * (w - 1) / (n - 1);
    int yy = y + h - 1 - (vv - vmin) * (h - 1) / (vmax - vmin);
    if (fill) g.vline(xx, yy, y + h - yy, mix(T.bg, k, 60));
    if (i) g.line(px, py, xx, yy, k);
    px = xx; py = yy;
  }
}

void icoGamepad(G &g, int cx, int cy, col_t k, col_t hole) {
  col_t hb = hole ? hole : T.bg; (void)hb;
  g.rrect(cx - 14, cy - 7, 28, 15, 7, k);
  g.disc(cx - 9, cy + 7, 5, k);
  g.disc(cx + 9, cy + 7, 5, k);
  g.rect(cx - 10, cy - 2, 7, 2, hb);
  g.rect(cx - 7, cy - 5, 2, 8, hb);
  g.disc(cx + 6, cy - 3, 2, hb);
  g.disc(cx + 10, cy + 1, 2, hb);
}

void icoPair(G &g, int cx, int cy, col_t k, col_t hole) {
  col_t hb = hole ? hole : T.bg; (void)hb;
  for (int d = 0; d < 2; d++) { g.ring(cx - 6, cy, 8 - d, k); g.ring(cx + 6, cy, 8 - d, k); }
  g.disc(cx, cy, 2, k);
}

void icoRadio(G &g, int cx, int cy, col_t k, col_t hole) {
  col_t hb = hole ? hole : T.bg; (void)hb;
  g.disc(cx, cy + 9, 2, k);
  for (int i = 0; i < 3; i++) arc(g, cx, cy + 9, 7 + i * 5, 3, -50, 50, mix(hb, k, 255 - i * 55));
}

void icoSpectrum(G &g, int cx, int cy, col_t k, col_t hole) {
  col_t hb = hole ? hole : T.bg; (void)hb;
  static const uint8_t hh[7] = {8, 14, 10, 22, 13, 17, 7};
  for (int i = 0; i < 7; i++) g.rrect(cx - 14 + i * 4, cy + 11 - hh[i], 3, hh[i], 1, mix(hb, k, 150 + hh[i] * 4));
}

void icoPower(G &g, int cx, int cy, col_t k, col_t hole) {
  col_t hb = hole ? hole : T.bg; (void)hb;
  g.tri(cx + 4, cy - 13, cx - 8, cy + 2, cx + 1, cy + 2, k);
  g.tri(cx - 4, cy + 13, cx + 8, cy - 2, cx - 1, cy - 2, k);
  g.rect(cx - 2, cy - 2, 5, 5, k);
}

void icoGear(G &g, int cx, int cy, col_t k, col_t hole) {
  col_t hb = hole ? hole : T.bg; (void)hb;
  g.disc(cx, cy, 9, k);
  for (int a = 0; a < 360; a += 45) {
    float t = a * 0.0174533f;
    g.disc(cx + (int)lroundf(11 * cosf(t)), cy + (int)lroundf(11 * sinf(t)), 3, k);
  }
  g.disc(cx, cy, 4, hb);
}

void icoInfo(G &g, int cx, int cy, col_t k, col_t hole) {
  col_t hb = hole ? hole : T.bg; (void)hb;
  g.disc(cx, cy, 12, k);
  g.disc(cx, cy, 10, hb);
  g.disc(cx, cy - 5, 2, k);
  g.rect(cx - 1, cy - 1, 3, 9, k);
}

void icoMoon(G &g, int cx, int cy, col_t k, col_t hole) {
  col_t hb = hole ? hole : T.bg; (void)hb;
  g.disc(cx, cy, 11, k);
  g.disc(cx + 6, cy - 4, 10, hb);
}

// heartbeat line: latency
void icoPulse(G &g, int cx, int cy, col_t k, col_t hole) {
  (void)hole;
  const int xs[8] = {-13, -6, -3, 0, 3, 6, 9, 13};
  const int ys[8] = {1, 1, -3, 10, -11, 2, 1, 1};
  for (int i = 0; i < 7; i++) {
    g.line(cx + xs[i], cy + ys[i], cx + xs[i + 1], cy + ys[i + 1], k);
    g.line(cx + xs[i], cy + ys[i] + 1, cx + xs[i + 1], cy + ys[i + 1] + 1, k);
  }
}

// three rows with bullets: event log
void icoList(G &g, int cx, int cy, col_t k, col_t hole) {
  (void)hole;
  for (int i = 0; i < 3; i++) {
    g.disc(cx - 10, cy - 8 + i * 8, 2, k);
    g.rrect(cx - 4, cy - 10 + i * 8, 16 - i * 3, 4, 2, k);
  }
}

// download arrow into a tray: update
void icoDown(G &g, int cx, int cy, col_t k, col_t hole) {
  (void)hole;
  g.rect(cx - 4, cy - 12, 9, 12, k);
  g.tri(cx - 11, cy - 1, cx + 11, cy - 1, cx, cy + 10, k);
  g.rect(cx - 12, cy + 12, 25, 3, k);
  g.rect(cx - 12, cy + 7, 3, 8, k);
  g.rect(cx + 10, cy + 7, 3, 8, k);
}
