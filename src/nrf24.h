#pragma once
#include "config.h"

// Minimal nRF24L01 driver, used only as a 2.4 GHz channel-occupancy scanner (it cannot talk to Xbox controllers).
// Shares the SPI bus with the screen, so it must only be called from the UI task.
bool nrf24_begin();                      // false if no module answers
bool nrf24_ok();
void nrf24_scan_slice(uint32_t budgetUs);  // scans the next few channels, updates the occupancy table
const uint8_t *nrf24_occupancy();        // 84 values, 0..100 %
const uint8_t *nrf24_peak();
void nrf24_sleep();                      // power the radio down
