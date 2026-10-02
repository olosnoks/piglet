#pragma once
#include <Arduino.h>

// Passive Pwnagotchi detector. Matches BLE adverts whose local name contains
// "pwnagotchi" or "pwngrid" (case-insensitive). Logs to
// /logs/pwnagotchi_<millis>.csv. Shares the NimBLE scan owner in
// BleDetectors.cpp; only one detector runs at a time.

void     pwnagotchiDetectBegin();
void     pwnagotchiDetectStart();
void     pwnagotchiDetectStop();
bool     pwnagotchiDetectActive();
uint32_t pwnagotchiDetectCount();
void     pwnagotchiDetectTick();
