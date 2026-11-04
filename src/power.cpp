#include "power.h"
#include <Wire.h>

#define BQ_ADDR 0x6B
#define PIN_SDA 8
#define PIN_SCL 18

static bool rd(uint8_t reg, uint8_t &v) {
  Wire.beginTransmission(BQ_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(BQ_ADDR, (uint8_t)1) != 1) return false;
  v = Wire.read();
  return true;
}

static void wr(uint8_t reg, uint8_t v) { Wire.beginTransmission(BQ_ADDR); Wire.write(reg); Wire.write(v); Wire.endTransmission(); }

void power_begin() {
  Wire.begin(PIN_SDA, PIN_SCL, 100000);
  uint8_t r02;
  if (rd(0x02, r02)) wr(0x02, r02 | 0x40);   // continuous ADC conversion, so battery/system voltages are real readings
}

bool power_report(char *buf, size_t n) {
  uint8_t r00, r0b, r0e, r0f, r11, r12;
  if (!rd(0x00, r00) || !rd(0x0b, r0b) || !rd(0x0e, r0e) || !rd(0x0f, r0f) || !rd(0x11, r11) || !rd(0x12, r12)) {
    snprintf(buf, n, "pmic: no answer");
    return false;
  }
  float vbat = 2.304f + (r0e & 0x7f) * 0.02f;
  float vsys = 2.304f + (r0f & 0x7f) * 0.02f;
  float vbus = (r11 & 0x7f) ? 2.6f + (r11 & 0x7f) * 0.1f : 0.0f;
  int chg = (r12 & 0x7f) * 50;
  int inlim = 100 + (r00 & 0x3f) * 50;
  static const char *cs[] = {"not-charging", "pre-charge", "fast-charge", "done"};
  snprintf(buf, n, "vbus=%.1fV vbat=%.2fV vsys=%.2fV chg=%dmA inlim=%dmA %s%s", vbus, vbat, vsys, chg, inlim,
           cs[(r0b >> 3) & 3], (r0b & 0x04) ? "" : " (power-NOT-good)");
  return true;
}

bool power_vbus_present() {
  uint8_t r0b;
  if (!rd(0x0b, r0b)) return true;       // can't tell: assume USB, which takes the safe (deep sleep) path
  return (r0b & 0xE0) != 0;              // VBUS_STAT != 000
}

void power_ship_mode() {
  uint8_t r09;
  if (rd(0x09, r09)) wr(0x09, (r09 | 0x20) & ~0x08);   // BATFET_DIS=1, BATFET_DLY=0 (turn off immediately)
}
