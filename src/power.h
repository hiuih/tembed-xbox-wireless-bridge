#pragma once
#include "config.h"

// Reads the BQ25896 charger/PMIC over I2C so we can see whether USB power is the problem.
void power_begin();
// Fills buf with e.g. "vbus=5.1V vbat=3.9V vsys=4.2V chg=300mA inlim=500mA stat=charging". Returns false if the chip doesn't answer.
bool power_report(char *buf, size_t n);
