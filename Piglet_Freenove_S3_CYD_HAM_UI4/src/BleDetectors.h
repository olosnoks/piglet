#pragma once
#include <Arduino.h>

// Shared coordination for the passive BLE detectors (AirTag Monitor, Flipper
// Sniff, Pwnagotchi Detect). Only one detector owns the NimBLE scan at a time;
// each mutually excludes the others plus the BLE spam subsystems.
//
// The per-detector public APIs live in AirTagMonitor.h / FlipperSniff.h /
// PwnagotchiDetect.h. All three are implemented in BleDetectors.cpp on top of a
// single shared scan owner so their scan callbacks never fight each other.

// True while any detector currently owns the BLE scan. The wardriving BLE
// scanner (BleScanner.cpp) checks this to yield the radio instead of restarting
// its own scan and clobbering the detector's callback.
bool bleDetectorsActive();
