#include "ScreenTelemetry.h"

ScreenTelemetryState screenTelemetry;

void screenTelemetryObserve(
    const String& ssid,
    const String& bssid,
    int rssi,
    int channel) {
  strlcpy(screenTelemetry.ssid, ssid.c_str(), sizeof(screenTelemetry.ssid));
  strlcpy(screenTelemetry.bssid, bssid.c_str(), sizeof(screenTelemetry.bssid));
  screenTelemetry.rssi = rssi;
  screenTelemetry.channel = channel;

  if (channel >= 1 && channel <= 14) {
    screenTelemetry.channelHits[channel]++;
  }
}

void screenTelemetryScanComplete(int found, uint32_t logged) {
  screenTelemetry.scanBatches++;
  screenTelemetry.lastScanFound = found;
  screenTelemetry.loggedRows += logged;
}
