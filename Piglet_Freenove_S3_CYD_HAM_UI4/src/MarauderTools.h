#pragma once
#include <Arduino.h>

enum class AttackMode : uint8_t {
  None = 0,
  BeaconSpam,
  Deauth,
  ProbeSniff,
  PcapCapture,
  RawCapture,
  DeauthDetect,   // passive: monitor for incoming deauth/disassoc frames
  Pmkid           // clientless PMKID capture (association flood + M1 sniff)
};

// Beacon-spam SSID source.
enum class BeaconSource : uint8_t { RickRoll = 0, Random = 1, Custom = 2 };

void marauderToolsBegin();
void marauderToolsTick();

// Configure beacon spam: which SSIDs to broadcast, an optional custom SSID, and
// whether to hop across 2.4 GHz channels instead of staying on one.
void         marauderSetBeaconConfig(BeaconSource src, const String& customSsid, bool channelHop);
BeaconSource marauderBeaconSource();
bool         marauderBeaconHop();

bool        marauderStartAttack(AttackMode mode, uint8_t channel);
void        marauderStopAttack();
bool        marauderAttackActive();
AttackMode  marauderCurrentAttack();
uint8_t     marauderCurrentChannel();

uint32_t marauderFramesSent();
uint32_t marauderFramesFailed();
uint32_t marauderFramesPerSec();
uint32_t marauderFramesCaptured();
uint32_t marauderFramesWritten();
String   marauderStatusLine();
String   marauderCurrentPcapPath();
String   marauderDeauthTargetStr();

void   marauderRefreshScanResults();
size_t marauderScanResultCount();
bool   marauderScanResultGet(size_t idx, String& ssid, String& bssid,
                             int& channel, int& rssi);

// ── Deauth targets ─────────────────────────────────────────────────────────
// Multi-target rotation: up to MAX_DEAUTH_TARGETS BSSIDs, cycled one per
// deauth frame, retuning the channel per target. The single-target setter is
// kept for backward compatibility and simply replaces the whole list.
void   marauderSetDeauthTarget(const String& bssid, uint8_t channel);
void   marauderAddDeauthTarget(const String& bssid, uint8_t channel);
void   marauderRemoveDeauthTarget(const String& bssid);
void   marauderClearDeauthTargets();
size_t marauderDeauthTargetCount();
bool   marauderDeauthTargetGet(size_t idx, String& bssid, uint8_t& channel);
bool   marauderDeauthTargetSelected(const String& bssid);

// ── PMKID capture ──────────────────────────────────────────────────────────
// PMKID capture is driven by the PmkidCapture module (PmkidCapture.h/.cpp).
// marauderStartAttack(AttackMode::Pmkid, ch) delegates to pmkidStart() using
// the target BSSID + SSID set via marauderSetPmkidTarget() below.
void marauderSetPmkidTarget(const String& bssid, const String& ssid, uint8_t channel);

String marauderPreflightError(AttackMode mode);