#pragma once

#define FIRMWARE_VERSION "v2.84-fnk0104b"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <TinyGPSPlus.h>

#include "StorageCompat.h"
#include "PinMapDefs.h"
#include "Config.h"

extern PinMap pins;
extern Config cfg;
extern TinyGPSPlus gps;
extern HardwareSerial GPSSerial;
extern WebServer server;

extern bool sdOk;
extern bool scanningEnabled;
extern bool gpsHasFix;
extern bool allowScanForOled;
extern bool userScanOverride;
extern bool autoPaused;

// True while the device is parked in OFF / radio-free mode: solo scan, BLE, and
// mesh (Core/Node) are all torn down and the Wi-Fi driver is idle so another RF
// tool can claim the radio. Cleared when any wardriving/mesh mode is re-entered.
extern bool radioIdle;

// Retained for backend compatibility. The TFT frontend owns visual navigation.
static const uint8_t PAGE_COUNT = 6;
extern uint8_t currentPage;
extern bool statusPagePaused;

extern uint32_t apStartMs;
extern bool apClientSeen;
extern bool apWindowActive;
extern const uint32_t AP_WINDOW_MS;
extern bool apExtended;
extern uint32_t apExtendedStartMs;
extern bool apForceClose;
extern const uint32_t AP_EXTENDED_WINDOW_MS;
extern const uint32_t AP_EXTEND_PROMPT_LEAD_MS;

extern uint32_t networksFound2G;
extern uint32_t networksFound5G;
extern uint32_t devicesFoundBle;

extern File logFile;
extern String currentCsvPath;
extern wl_status_t lastStaStatus;

extern bool uploading;
extern bool uploadPausedScanWasEnabled;
extern uint32_t uploadTotalFiles;
extern uint32_t uploadDoneFiles;
extern String uploadCurrentFile;
extern String uploadLastResult;
extern String uploadTargetName;
extern uint32_t uploadFailedFiles;
extern int wigleTokenStatus;
extern int wigleLastHttpCode;

extern const char* WIGLE_HOST;
extern const uint16_t WIGLE_PORT;

inline void tlsMaybeSetBufferSizes(WiFiClientSecure&, int, int) {}
