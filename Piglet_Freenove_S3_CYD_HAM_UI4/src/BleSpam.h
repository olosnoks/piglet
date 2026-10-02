#pragma once
#include <Arduino.h>

// Multi-variant BLE advertisement spam. Broadcasts spoofed pairing
// advertisements that trigger popups on nearby devices. Each variant targets a
// different OS/ecosystem:
//
//   Apple     — iOS/macOS AirPods/AppleTV/HomePod pairing popups
//   SwiftPair — Windows 10/11 "Add a device" popups
//   Samsung   — Samsung Galaxy Watch/Buds pairing popups
//   FastPair  — Google/Android Fast Pair popups
//
// Uses the existing NimBLE stack (shared with BleScanner / BleDetectors).
// Only one spam variant runs at a time; starting a new one stops the current.
// Refuses to start while mesh Core/Node is active.

enum class BleSpamType : uint8_t {
  None = 0,
  Apple,
  SwiftPair,
  Samsung,
  FastPair
};

void        bleSpamBegin();       // call once in setup()
void        bleSpamStart(BleSpamType type);
void        bleSpamStop();
bool        bleSpamActive();
BleSpamType bleSpamCurrentType();
uint32_t    bleSpamCount();       // advertisements sent this session
void        bleSpamTick();        // call every loop()

const char* bleSpamTypeName(BleSpamType type);