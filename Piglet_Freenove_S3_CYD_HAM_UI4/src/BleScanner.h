#pragma once

#include <Arduino.h>

// Passive BLE wardriving scanner. Logs advertising devices to the active
// WiGLE CSV (Type=BLE) so they upload to WDGoWars for extra points.
//
// Runs concurrently with the WiFi scan by time-slicing the shared 2.4 GHz
// radio. Automatically idle in mesh modes and when scanning is disabled.

void bleScannerBegin();     // Call once in setup()
void bleScannerTick();      // Call every loop(); starts/stops scan and drains results
void bleScannerShutdown();  // Fully release the BT controller (call before entering ESP-NOW mesh)

bool     bleScannerActive();  // True while a scan is running
uint32_t bleUniqueCount();    // Distinct BLE MACs logged this session
