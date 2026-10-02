#pragma once

#include <Arduino.h>

struct ScreenTelemetryState {
  char ssid[33] = {};
  char bssid[18] = {};
  int rssi = 0;
  int channel = 0;

  uint32_t scanBatches = 0;
  uint32_t loggedRows = 0;
  int lastScanFound = 0;
  uint32_t channelHits[15] = {};
};

extern ScreenTelemetryState screenTelemetry;

void screenTelemetryObserve(
    const String& ssid,
    const String& bssid,
    int rssi,
    int channel);

void screenTelemetryScanComplete(int found, uint32_t logged);
