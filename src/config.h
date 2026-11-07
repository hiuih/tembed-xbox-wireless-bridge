#pragma once
#include <Arduino.h>

#define FW_VERSION "1.4"
#define UPDATE_REPO "hiuih/tembed-xbox-wireless-bridge"

// Max simultaneous controllers (xone's dongle supports 16; USB descriptor budget limits us to 8).
#define MAX_PADS 8

// Fake dongle MAC. Controllers remember this address, so do NOT change it
// or every controller has to be re-paired.
static const uint8_t DONGLE_MAC[6] = {0x62, 0x45, 0xbd, 0x00, 0x00, 0x01};

// 2.4 GHz channels the dongle protocol uses.
static const uint8_t SCAN_CHANNELS[] = {1, 6, 11};
#define SCAN_DWELL_MS 3000

// Drop a controller if nothing is heard from it for this long.
#define PAD_TIMEOUT_MS 4000

// T-Embed CC1101 pins (from LilyGO's pin map).
#define PIN_PWR_EN 15
#define PIN_LCD_CS 41
#define PIN_LCD_DC 16
#define PIN_LCD_BL 21
#define PIN_SPI_SCK 11
#define PIN_SPI_MOSI 9
#define PIN_SPI_MISO 10
#define PIN_ENC_A 4
#define PIN_ENC_B 5
#define PIN_ENC_KEY 0
// Board variants. T-Embed (default): screen + 8-LED ring + knob + charger chip.
// Headless (-DHEADLESS=1): a bare ESP32-S3 board: no screen/ring/charger; one optional RGB LED, the BOOT button
// (GPIO0) as an optional pair button, and a pairing window that opens automatically at power-up.
#ifdef HEADLESS
  #define HAS_SCREEN 0
  #define HAS_PMIC 0
  #ifndef LED_COUNT
    #define LED_COUNT 1
  #endif
  #ifndef LED_PIN
    #define LED_PIN 48            // ESP32-S3-DevKitC-1 v1.0 RGB LED (v1.1 boards use 38): -DLED_PIN=38
  #endif
  #ifndef AUTO_PAIR_BOOT_MS
    #define AUTO_PAIR_BOOT_MS 120000
  #endif
#else
  #define HAS_SCREEN 1
  #define HAS_PMIC 1
  #define LED_COUNT 8
  #define LED_PIN 14              // 8x WS2812 ring around the knob
#endif
#define PIN_LED LED_PIN
#define PIN_CC1101_CS 12
#define PIN_SD_CS 13

// The visible part of the (partly damaged) display is the left side: lay the UI out in this width.
#ifndef UI_W
#define UI_W 176   // damaged-screen layout; a healthy display uses -DUI_W=320
#endif
#define UI_H 170
