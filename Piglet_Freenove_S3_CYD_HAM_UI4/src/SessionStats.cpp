#include "SessionStats.h"

#include "Globals.h"

uint32_t securityTally[SEC_COUNT] = {};
const char* const SEC_LABELS[SEC_COUNT] = {"OPEN", "WEP", "WPA", "WPA2", "WPA3", "ENT", "OTHER"};

void statsRecordWifiAuth(wifi_auth_mode_t m) {
  switch (m) {
    case WIFI_AUTH_OPEN:            securityTally[SEC_OPEN]++; break;
    case WIFI_AUTH_WEP:             securityTally[SEC_WEP]++;  break;
    case WIFI_AUTH_WPA_PSK:         securityTally[SEC_WPA]++;  break;
    case WIFI_AUTH_WPA2_PSK:
    case WIFI_AUTH_WPA_WPA2_PSK:    securityTally[SEC_WPA2]++; break;
    case WIFI_AUTH_WPA3_PSK:
    case WIFI_AUTH_WPA2_WPA3_PSK:   securityTally[SEC_WPA3]++; break;
    case WIFI_AUTH_WPA2_ENTERPRISE: securityTally[SEC_ENT]++;  break;
    default:                        securityTally[SEC_OTHER]++; break;
  }
}

// ---- Discovery history: cumulative WiFi/BLE totals sampled over time ----
namespace {
constexpr int CAP = 90;
uint32_t histWifi[CAP] = {};
uint32_t histBle[CAP]  = {};
int      histLen = 0;
uint32_t histIntervalMs = 5000;  // doubles as the buffer fills, keeping the whole session in view
uint32_t lastSampleMs = 0;
}

void statsHistoryTick() {
  const uint32_t now = millis();
  if (histLen > 0 && (now - lastSampleMs) < histIntervalMs) return;
  lastSampleMs = now;

  // Buffer full: halve the resolution (keep every other sample) and slow the cadence.
  if (histLen >= CAP) {
    for (int i = 0; i < CAP / 2; ++i) {
      histWifi[i] = histWifi[i * 2];
      histBle[i]  = histBle[i * 2];
    }
    histLen = CAP / 2;
    histIntervalMs *= 2;
  }

  histWifi[histLen] = networksFound2G + networksFound5G;
  histBle[histLen]  = devicesFoundBle;
  histLen++;
}

// ---- Radios Per Minute: rolling-window detection rate ----
namespace {
constexpr int   RCAP = 16;          // 16 samples * 2s = ~30s window
constexpr uint32_t RATE_INTERVAL_MS = 2000;
uint32_t rMs[RCAP]  = {};
uint32_t rTot[RCAP] = {};
int      rIdx = 0;                   // next write slot
int      rLen = 0;
uint32_t rLastMs = 0;
}

void statsRateTick() {
  const uint32_t now = millis();
  if (rLen > 0 && (now - rLastMs) < RATE_INTERVAL_MS) return;
  rLastMs = now;
  rMs[rIdx]  = now;
  rTot[rIdx] = networksFound2G + networksFound5G + devicesFoundBle;
  rIdx = (rIdx + 1) % RCAP;
  if (rLen < RCAP) rLen++;
}

float radiosPerMinute() {
  if (rLen < 2) return 0.0f;
  const int newest = (rIdx - 1 + RCAP) % RCAP;
  const int oldest = (rIdx - rLen + RCAP) % RCAP;
  const uint32_t dms = rMs[newest] - rMs[oldest];
  if (dms == 0) return 0.0f;
  const uint32_t dc = rTot[newest] - rTot[oldest];
  return (float)dc * 60000.0f / (float)dms;
}

int statsHistoryLen() { return histLen; }

void statsHistoryGet(int index, uint32_t& wifi, uint32_t& ble) {
  if (index < 0 || index >= histLen) { wifi = 0; ble = 0; return; }
  wifi = histWifi[index];
  ble  = histBle[index];
}
