#pragma once

#include <Arduino.h>
#include <WiFi.h>

// Lightweight session stats for the STATS screen visualizers.
// Quick-and-dirty: security is tallied per observation (not per unique BSSID),
// and the discovery history tracks cumulative observation totals over time.

enum SecCat { SEC_OPEN, SEC_WEP, SEC_WPA, SEC_WPA2, SEC_WPA3, SEC_ENT, SEC_OTHER, SEC_COUNT };

extern uint32_t securityTally[SEC_COUNT];
extern const char* const SEC_LABELS[SEC_COUNT];

void statsRecordWifiAuth(wifi_auth_mode_t mode);  // Call once per scanned WiFi row

void statsHistoryTick();                           // Call every loop(); samples on a timer
int  statsHistoryLen();
void statsHistoryGet(int index, uint32_t& wifi, uint32_t& ble);

// "Radios Per Minute" — rate of WiFi+BLE detections over a recent rolling window.
void  statsRateTick();        // Call every loop(); samples on a timer
float radiosPerMinute();
