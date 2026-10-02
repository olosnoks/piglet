#pragma once
#include <Arduino.h>

// Raw management/data-frame capture with per-subtype counters. This is a thin
// facade over the existing MarauderTools promiscuous-capture engine: it adds an
// AttackMode::RawCapture path that counts frames by 802.11 subtype (and EAPOL in
// data frames) and exposes a compact breakdown for the UI. No PCAP file is
// written — a one-line summary is logged to /logs/raw_<millis>.csv on stop.

enum class RawCaptureFilter : uint8_t {
  AllMgmt,   // all management subtypes
  Beacons,
  Probes,    // probe request + response
  Deauth,    // deauth + disassoc
  Eapol      // EAPOL (in data frames)
};

void     rawCaptureBegin();
void     rawCaptureStart(RawCaptureFilter filter, uint8_t channel);
void     rawCaptureStop();
bool     rawCaptureActive();
uint32_t rawCaptureFrames();
String   rawCaptureBreakdown();   // e.g. "B:123 P:45 D:6 E:2"
void     rawCaptureTick();

RawCaptureFilter rawCaptureCurrentFilter();

// Hook called from the MarauderTools promiscuous RX callback while a raw capture
// is active. Kept in IRAM so it is safe to call from the WiFi RX context.
void IRAM_ATTR rawCaptureOnFrame(const uint8_t* frame, uint16_t len, bool isData);

// True when the raw-capture filter needs data frames (EAPOL). MarauderTools uses
// this to widen the promiscuous filter mask.
bool rawCaptureWantsData();
