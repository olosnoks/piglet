#pragma once
#include <Arduino.h>

// WPS vulnerability check. The Arduino WiFiScan API does not expose the WPS bit
// for scanned APs, so this module runs its own blocking ESP-IDF scan
// (esp_wifi_scan_get_ap_records) and reads the `wps` flag from each
// wifi_ap_record_t. Results are cached until the next scan.

// Run a fresh WPS scan. Returns the number of APs found, or -1 if it could not
// run (mesh/attack active, or the scan failed). Refuses while a Marauder attack
// or mesh mode owns the radio.
int  wpsScanRun();

// Number of APs / WPS-enabled APs from the last scan.
size_t wpsApCount();
size_t wpsEnabledCount();

// Fetch one cached result by index (0-based). Returns false if out of range.
bool wpsGet(size_t index, String& ssid, String& bssid, int& channel, int& rssi, bool& wps);

// True if the AP at this index (into the last scan) has WPS enabled.
bool wpsCheckAp(int index);

// Human-readable label for a scan result at this index ("WPS" / "-").
const char* wpsLabel(int index);

// Append the last scan's results to /logs/wps_<millis>.csv (bssid,ssid,wps).
// Returns true on success.
bool wpsLogResults();
