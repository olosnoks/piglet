#pragma once

#include <Arduino.h>

// LiPo voltage monitor on cfg.battPin (1:2 divider; GPIO 9 on the FNK0104B).

void batteryMonitorTick();      // Call every loop(); samples once per second
bool batteryMonitorAvailable(); // True once a valid pin has produced a reading
float batteryVoltage();         // Smoothed cell voltage in volts
int batteryPercent();           // Approximate state of charge, 0-100
