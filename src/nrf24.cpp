#include "nrf24.h"
#include <SPI.h>

#define NRF_CE 43
#define NRF_CSN 44
#define NCH 84

static bool present = false;
static uint8_t occ[NCH], pk[NCH];
static uint8_t nextCh = 0;
static uint32_t lastDecay = 0;

static uint8_t rd(uint8_t reg) {
  SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
  digitalWrite(NRF_CSN, LOW);
  SPI.transfer(reg & 0x1F);
  uint8_t v = SPI.transfer(0xFF);
  digitalWrite(NRF_CSN, HIGH);
  SPI.endTransaction();
  return v;
}

static void wr(uint8_t reg, uint8_t val) {
  SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
  digitalWrite(NRF_CSN, LOW);
  SPI.transfer(0x20 | (reg & 0x1F));
  SPI.transfer(val);
  digitalWrite(NRF_CSN, HIGH);
  SPI.endTransaction();
}

bool nrf24_begin() {
  // GPIO43/44 double as UART0; take them over as plain outputs
  pinMode(NRF_CE, OUTPUT); digitalWrite(NRF_CE, LOW);
  pinMode(NRF_CSN, OUTPUT); digitalWrite(NRF_CSN, HIGH);
  delay(5);
  wr(0x00, 0x03);                        // CONFIG: power up, receiver
  delay(5);
  wr(0x03, 0x02);                        // SETUP_AW: probe register
  present = (rd(0x03) == 0x02);
  if (present) {
    wr(0x01, 0x00);                      // no auto-ack
    wr(0x02, 0x00);                      // no pipes
    wr(0x03, 0x03);
    wr(0x06, 0x0F);                      // 2 Mbps (widest receive filter)
  } else {
    digitalWrite(NRF_CSN, HIGH);
  }
  return present;
}

bool nrf24_ok() { return present; }

void nrf24_scan_slice(uint32_t budgetUs) {
  if (!present) return;
  uint32_t t0 = micros();
  while (micros() - t0 < budgetUs) {
    uint8_t ch = nextCh;
    nextCh = (nextCh + 1) % NCH;
    wr(0x05, ch);
    uint8_t hits = 0;
    for (int k = 0; k < 12; k++) {
      digitalWrite(NRF_CE, HIGH);
      delayMicroseconds(130);            // RX settle + carrier detect window
      hits += rd(0x09) & 1;              // RPD: carrier above about -64 dBm
      digitalWrite(NRF_CE, LOW);
    }
    uint8_t now = hits * 100 / 12;
    occ[ch] = (occ[ch] * 6 + now * 4) / 10;       // smooth
    if (occ[ch] > pk[ch]) pk[ch] = occ[ch];
  }
  if (millis() - lastDecay > 400) {       // peak markers fall slowly
    lastDecay = millis();
    for (int i = 0; i < NCH; i++) if (pk[i] > occ[i]) pk[i]--;
  }
}

const uint8_t *nrf24_occupancy() { return occ; }
const uint8_t *nrf24_peak() { return pk; }

void nrf24_sleep() {
  if (!present) return;
  digitalWrite(NRF_CE, LOW);
  wr(0x00, 0x00);
}
