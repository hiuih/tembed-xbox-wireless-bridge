#pragma once
// Small drawing layer on top of Adafruit_GFX: colours, rounded cards, icons, text helpers.
// All drawing goes through G so a frame can be rendered in horizontal bands (half the RAM of a full canvas).
#include <Adafruit_GFX.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>

typedef uint16_t col_t;
static inline col_t rgb(uint8_t r, uint8_t g, uint8_t b) { return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3); }
col_t mix(col_t a, col_t b, uint8_t t);        // t = 0..255 (0 = a, 255 = b)

struct Theme { col_t bg, card, cardHi, border, text, dim, accent, accentDim, good, warn, bad, blue, purple; };
extern Theme T;
void theme_set(uint8_t accentIndex);           // 0 green, 1 blue, 2 purple, 3 orange

#define FONT_S  nullptr                        // built-in 6x8
#define FONT_M  (&FreeSans9pt7b)
#define FONT_B  (&FreeSansBold9pt7b)
#define FONT_L  (&FreeSansBold12pt7b)
#define FONT_XL (&FreeSansBold18pt7b)

struct G {
  GFXcanvas16 *c;
  int oy;                                      // top row of this band within the full frame
  void px(int x, int y, col_t k) { c->drawPixel(x, y - oy, k); }
  void rect(int x, int y, int w, int h, col_t k) { c->fillRect(x, y - oy, w, h, k); }
  void frame(int x, int y, int w, int h, col_t k) { c->drawRect(x, y - oy, w, h, k); }
  void rrect(int x, int y, int w, int h, int r, col_t k) { c->fillRoundRect(x, y - oy, w, h, r, k); }
  void rframe(int x, int y, int w, int h, int r, col_t k) { c->drawRoundRect(x, y - oy, w, h, r, k); }
  void hline(int x, int y, int w, col_t k) { c->drawFastHLine(x, y - oy, w, k); }
  void vline(int x, int y, int h, col_t k) { c->drawFastVLine(x, y - oy, h, k); }
  void line(int x0, int y0, int x1, int y1, col_t k) { c->drawLine(x0, y0 - oy, x1, y1 - oy, k); }
  void disc(int x, int y, int r, col_t k) { c->fillCircle(x, y - oy, r, k); }
  void ring(int x, int y, int r, col_t k) { c->drawCircle(x, y - oy, r, k); }
  void tri(int x0, int y0, int x1, int y1, int x2, int y2, col_t k) { c->fillTriangle(x0, y0 - oy, x1, y1 - oy, x2, y2 - oy, k); }
  // text: with a FreeFont (FONT_M/B/L/XL) y is the baseline; with FONT_S (built-in) y is the top edge
  void text(int x, int y, const char *s, col_t k, const GFXfont *f = FONT_M);
  int textW(const char *s, const GFXfont *f = FONT_M);
  void textC(int cx, int y, const char *s, col_t k, const GFXfont *f = FONT_M) { text(cx - textW(s, f) / 2, y, s, k, f); }
  void textR(int rx, int y, const char *s, col_t k, const GFXfont *f = FONT_M) { text(rx - textW(s, f), y, s, k, f); }
};

// composite helpers
void vgrad(G &g, int x, int y, int w, int h, col_t top, col_t bottom);
void hgrad(G &g, int x, int y, int w, int h, col_t left, col_t right);
void card(G &g, int x, int y, int w, int h, bool selected, col_t fill = 0);
void pill(G &g, int x, int y, int w, int h, col_t fill, const char *s, col_t txt, const GFXfont *f = FONT_B);
void arc(G &g, int cx, int cy, int r, int thick, int a0, int a1, col_t k);   // degrees, 0 = up, clockwise
void bar(G &g, int x, int y, int w, int h, float frac, col_t fg, col_t bg);
void batteryIcon(G &g, int x, int y, int pct, bool charging, bool known = true);
void signalBars(G &g, int x, int y, int rssi);
void usbIcon(G &g, int x, int y, col_t k);
void sparkline(G &g, int x, int y, int w, int h, const uint16_t *v, int n, uint16_t vmin, uint16_t vmax, col_t k, bool fill);

// icons, drawn centred on (cx, cy) in roughly a 28x28 box
void icoPulse(G &g, int cx, int cy, col_t k, col_t hole = 0);
void icoList(G &g, int cx, int cy, col_t k, col_t hole = 0);
void icoGamepad(G &g, int cx, int cy, col_t k, col_t hole = 0);
void icoPair(G &g, int cx, int cy, col_t k, col_t hole = 0);
void icoRadio(G &g, int cx, int cy, col_t k, col_t hole = 0);
void icoSpectrum(G &g, int cx, int cy, col_t k, col_t hole = 0);
void icoPower(G &g, int cx, int cy, col_t k, col_t hole = 0);
void icoGear(G &g, int cx, int cy, col_t k, col_t hole = 0);
void icoInfo(G &g, int cx, int cy, col_t k, col_t hole = 0);
void icoMoon(G &g, int cx, int cy, col_t k, col_t hole = 0);
