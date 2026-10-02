#pragma once
#include <Arduino.h>

// ================================================================
//  MeshNode — JCMK-compatible ESP-Now wardriving node (page 5)
//  Compatible with justcallmekoko/ESP32DualBandWardriver core role.
// ================================================================

// JCMK channel table (shared so Display can show range labels)
extern const uint8_t JCMK_ESPNOW_CH;
extern const uint8_t JCMK_NUM_CHANNELS;
extern const uint8_t JCMK_CHANNELS[];

// Node state (read from Display.cpp to render page 5)
extern bool     meshNodeActive;
extern bool     jcmkHaveCore;
extern uint8_t  jcmkCoreMac[6];
extern uint8_t  jcmkAssignVer;
extern uint8_t  jcmkStartIdx;
extern uint8_t  jcmkEndIdx;
extern uint32_t jcmkNetworksFound;
extern uint32_t jcmkSentCount;

// Lifecycle — called from Piglet.ino on page enter/exit
void enterNodeMode();
void exitNodeMode();

// Loop tick — call every loop() iteration while on page 5
void nodeModeTick();

// OLED page renderer — called from Display.cpp updateOLED() (handles both modes)
void drawPageMeshNode();

// ================================================================
//  MeshCore — Core (coordinator) role
//  Long-press on page 5 while in Node mode activates Core mode;
//  long-press again returns to Node mode.
// ================================================================

#define CORE_MAX_NODES 12

struct CoreNodeInfo {
  bool     active;
  uint8_t  mac[6];
  uint8_t  startIdx;   // index into JCMK_CHANNELS[]
  uint8_t  endIdx;
  uint32_t lastHbMs;
  uint32_t recordsRx;
  bool     isBiscuit;  // true = Biscuit Node protocol (requires full-size 212-byte packets)
};

// Core state (read from Display.cpp for page 5 rendering)
extern bool          meshCoreActive;
extern uint32_t      coreRecordsRx;
extern uint8_t       coreNodeCount;
extern CoreNodeInfo  coreNodes[CORE_MAX_NODES];

void enterCoreMode();
void exitCoreMode();
void coreModeTick();

// ================================================================
//  Biscuit Reporter — solo-mode telemetry to a Biscuit Manager/Core
//  When cfg.biscuitReport is on and the device is wardriving solo, it
//  announces itself and forwards found networks over ESP-NOW (ch 6) so
//  a Biscuit Manager lists it as a biscuit. Local SD logging continues.
// ================================================================
extern bool     biscuitReporterActive;  // ESP-NOW up and reporting
extern bool     biscuitLinked;          // a Core/manager has replied
extern uint32_t biscuitRecordsSent;

void biscuitForwardNetwork(const String& bssid, const String& ssid,
                           const String& auth, int ch, int rssi);  // buffer one row (from scanner)
void biscuitReporterFlush();  // send buffered rows + heartbeat on ch 6 (radio-idle point)
void biscuitReporterReset();  // relinquish the ESP-NOW driver to Core/Node mode
