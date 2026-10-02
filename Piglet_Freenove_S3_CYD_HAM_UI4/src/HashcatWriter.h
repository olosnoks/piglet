#pragma once
#include <Arduino.h>

// Parses EAPOL-Key frames from the promiscuous RX stream and writes Hashcat
// mode 22000 (.hc22000) lines for crackable 4-way handshakes and PMKIDs.
//
// Feeds off the same data-frame stream that the PCAP capture writes. Every
// EAPOL frame the promiscuous callback sees is handed to hashcatWriterObserve()
// which parses the 4-way handshake state machine per (AP, STA) pair and emits a
// line once a complete handshake or a PMKID is observed.
//
// Output: /logs/hashcat_<millis>.hc22000, one line per crackable material.
//
// File format (Hashcat 22000):
//   WPA*01*PMKID*AP*STA*ESSID***                     (PMKID, TYPE=01)
//   WPA*02*MIC*AP*STA*ESSID*ANONCE*EAPOL*MESSAGEPAIR (handshake, TYPE=02)
//
// All MACs are 12 hex chars with no separators; ESSID/EAPOL/ANONCE are hex
// encoded. MESSAGEPAIR is a bitmask: 0x00 = M1+M2, 0x02 = M2+M3, 0x04 = M3+M4.

void     hashcatWriterBegin();
bool     hashcatWriterOpen(const String& ssidHint);   // opens a fresh file
void     hashcatWriterClose();
bool     hashcatWriterIsOpen();
uint32_t hashcatWriterHandshakeCount();
uint32_t hashcatWriterPmkidCount();
String   hashcatWriterCurrentPath();

// Called from the promiscuous callback for every data frame while PCAP capture
// is active. `frame` points at the 802.11 header; `len` is the full frame
// length. Kept IRAM-safe: does no SD I/O itself, only buffers parsed state.
void IRAM_ATTR hashcatWriterObserve(const uint8_t* frame, uint16_t len);

// Called from the main loop (or marauderToolsTick) to flush any completed
// handshake that was detected in the RX callback. Doing the actual file write
// on the main loop keeps the callback fast.
void     hashcatWriterTick();