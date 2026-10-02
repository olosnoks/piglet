#pragma once
#include <Arduino.h>

// Passive Flipper Zero detector. Matches BLE adverts whose local name contains
// "Flipper" (case-insensitive) or whose manufacturer data begins with the
// Flipper identifier (0x81 0x30). Logs to /logs/flippers_<millis>.csv. Shares
// the NimBLE scan owner in BleDetectors.cpp; only one detector runs at a time.

void     flipperSniffBegin();
void     flipperSniffStart();
void     flipperSniffStop();
bool     flipperSniffActive();
uint32_t flipperSniffCount();
void     flipperSniffTick();
