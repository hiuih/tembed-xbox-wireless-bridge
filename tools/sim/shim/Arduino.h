#pragma once
// Minimal Arduino shim so Adafruit_GFX and the UI drawing code build on a desktop (screen simulator only).
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <string>
#include <algorithm>
using std::min;
using std::max;
typedef uint8_t byte;
typedef bool boolean;
#define PROGMEM
#define PSTR(x) x
class __FlashStringHelper;
#define F(x) (reinterpret_cast<const __FlashStringHelper *>(PSTR(x)))
inline void optimistic_yield(uint32_t) {}
inline unsigned long millis() { return 0; }
inline void delay(unsigned long) {}
class String {
  std::string s;
public:
  String(const char *c = "") : s(c) {}
  const char *c_str() const { return s.c_str(); }
  size_t length() const { return s.size(); }
  char charAt(size_t i) const { return s[i]; }
};
#include "Print.h"
#define radians(deg) ((deg) * 0.017453292519943295)
#define degrees(rad) ((rad) * 57.29577951308232)
