#pragma once
#include <Arduino.h>

// Clientless PMKID capture.
//
// Sends 802.11 association requests to a target AP and parses the EAPOL-Key
// M1 response for the PMKID KDE. No client needs to be connected — the PMKID
// is returned by the AP during the initial association handshake and is
// derivable from the pre-shared key, so a captured PMKID cracks offline with
// hashcat -m 22000 (TYPE=01) exactly like a 4-way handshake.
//
// Reference (802.11-2016 §12.7.2 RSN capabilities, and the "PMKID attack" as
// popularised by hashcat):
//   PMKID = HMAC-SHA1-128(PMK, "PMK Name" || AA || SPA)
//   PMKID is delivered in an RSN vendor-specific KDE inside M1's key data:
//     00 0F AC 04 | length=0x10 | 16 bytes of PMKID
//
// Session lifecycle:
//   pmkidStart() saves the WiFi state, rebuilds the driver in APSTA mode (so
//   raw frames can be injected on WIFI_IF_AP), locks the channel, installs the
//   promiscuous filter that watches data frames, and begins sending association
//   requests on a fixed cadence.
//
//   pmkidTick() runs on the main loop, drains captured PMKIDs to SD, and
//   auto-stops after PMKID_AUTO_STOP_MS of no successful capture.
//
// Output: /logs/pmkid_<millis>.hc22000, one line per captured PMKID.

// How long to keep trying before giving up (milliseconds). Matches Marauder's
// default. The module stops itself and reports back via pmkidStopReason().
constexpr uint32_t PMKID_AUTO_STOP_MS = 60000;

void     pmkidBegin();
bool     pmkidStart(const String& targetBssid, const String& targetSsid, uint8_t channel);
void     pmkidStop();
bool     pmkidActive();
void     pmkidTick();

// Counters for the UI.
uint32_t pmkidAttempts();       // association requests sent
uint32_t pmkidCaptures();       // successful PMKIDs extracted
String   pmkidCurrentPath();    // path to the open .hc22000 file (empty if none)
String   pmkidStatusLine();     // one-line status: "12 attempts  1 capture"

// Reason the module last stopped on its own. Empty string if stopped by user.
String   pmkidStopReason();

// Called from MarauderTools's promiscuous RX callback with each data frame
// while pmkidActive() is true. Kept IRAM-safe; the actual SD writes happen in
// pmkidTick().
void IRAM_ATTR pmkidOnFrame(const uint8_t* frame, uint16_t len, int8_t rssi);