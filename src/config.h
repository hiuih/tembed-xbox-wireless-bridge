#pragma once
#include <Arduino.h>

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
#define PIN_LED 14        // 8x WS2812 ring around the knob
#define PIN_CC1101_CS 12
#define PIN_SD_CS 13

// The visible part of the (partly damaged) display is the left side: lay the UI out in this width.
#define UI_W 176
#define UI_H 170
