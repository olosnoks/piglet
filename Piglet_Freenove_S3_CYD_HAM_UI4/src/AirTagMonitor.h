#pragma once
#include <Arduino.h>

// Passive AirTag detector. Watches BLE advertisements for Apple Find My network
// patterns (Apple company ID 0x004C, Find My message type 0x12) and logs
// sightings to /logs/airtags_<millis>.csv. Shares the NimBLE scan owner in
// BleDetectors.cpp with the other detectors; only one runs at a time.

void     airTagMonitorBegin();
void     airTagMonitorStart();
void     airTagMonitorStop();
bool     airTagMonitorActive();
uint32_t airTagMonitorCount();
void     airTagMonitorTick();
