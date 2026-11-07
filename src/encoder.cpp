#include "encoder.h"
#include "driver/gpio.h"
#include "soc/gpio_struct.h"

#define TRANSITIONS_PER_CLICK 2

static volatile int32_t rawPos = 0;     // valid transitions, signed
static volatile uint8_t ab = 0;         // last (A << 1) | B
static int32_t handed = 0;              // clicks already returned to the caller
static bool reversed = false;

// index = (old << 2) | new. +1 along the sequence 00 -> 01 -> 11 -> 10 -> 00, -1 the other way,
// 0 for "no change" and for double-bit jumps (a missed edge or bounce)
static const int8_t kStep[16] DRAM_ATTR = {0, +1, -1, 0, -1, 0, 0, +1, +1, 0, 0, -1, 0, -1, +1, 0};

static void IRAM_ATTR onEdge() {
  // read the input register directly: nothing here may live in flash (the cache can be off during flash writes)
  uint32_t in = GPIO.in;
  uint8_t now = (((in >> PIN_ENC_A) & 1) << 1) | ((in >> PIN_ENC_B) & 1);
  uint8_t old = ab;
  if (now == old) return;
  ab = now;                              // always resynchronise, even on an invalid jump
  int8_t d = kStep[(old << 2) | now];
  if (d) rawPos += d;
}

void encoder_begin() {
  pinMode(PIN_ENC_A, INPUT_PULLUP);
  pinMode(PIN_ENC_B, INPUT_PULLUP);
  ab = (digitalRead(PIN_ENC_A) << 1) | digitalRead(PIN_ENC_B);
  attachInterrupt(PIN_ENC_A, onEdge, CHANGE);
  attachInterrupt(PIN_ENC_B, onEdge, CHANGE);
}

static int32_t floorDiv(int32_t a, int32_t b) {
  int32_t q = a / b;
  if ((a % b != 0) && ((a < 0) != (b < 0))) q--;
  return q;
}

int encoder_take() {
  int32_t pos = floorDiv(rawPos, TRANSITIONS_PER_CLICK);
  int32_t d = pos - handed;
  handed = pos;
  return reversed ? d : -d;              // positive raw sequence is counter-clockwise on this board
}

void encoder_set_reverse(bool reverse) { reversed = reverse; }
