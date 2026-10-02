#pragma once
#include <Arduino.h>

// Passive deauthentication / disassociation monitor (WIDS-style). Listens on a
// locked channel for 802.11 deauth (subtype 0x0C) and disassoc (0x0A) frames,
// counts them, tracks unique source MACs, logs to SD, and raises an alert when
// the rate spikes (someone is running a deauth attack nearby).
//
// Receive-only: this module never transmits. It runs as an AttackMode so it
// reuses the existing channel-lock and WiFi-state save/restore in MarauderTools.

void     deauthDetectorBegin();
bool     deauthDetectorStart(uint8_t channel);
void     deauthDetectorStop();
bool     deauthDetectorActive();
uint32_t deauthDetectorCount();          // total deauth/disassoc seen this session
uint32_t deauthDetectorUniqueSources();  // distinct source MACs seen
void     deauthDetectorTick();           // drain + log (called from marauderToolsTick)
String   deauthDetectorStatusLine();     // e.g. "src 5   10s 40"
bool     deauthDetectorAlert();          // true while the recent-rate threshold is exceeded

// Hook called from the MarauderTools promiscuous RX callback for each management
// frame while the detector is active. Kept in IRAM for the WiFi RX context.
void IRAM_ATTR deauthDetectorOnFrame(const uint8_t* frame, uint16_t len, int8_t rssi);
