#pragma once
#include <Arduino.h>
#include "PinMapDefs.h"

struct Config {
  String wigleBasicToken;
  String homeSsid;
  String homePsk;
  String wardriverSsid = "Piglet-WARDRIVE";
  String wardriverPsk  = "wardrive1234";
  uint32_t gpsBaud     = 9600;
  String scanMode      = "aggressive"; // aggressive | powersaving
  bool bluetoothScan   = true;         // Passively scan BLE devices and log them to the WiGLE CSV (Type=BLE) for extra WDGoWars points.
  bool scan24          = true;         // Log 2.4 GHz Wi-Fi networks while wardriving.
  bool scan5           = true;         // Log 5 GHz Wi-Fi networks (only effective on 5 GHz-capable boards, e.g. XIAO C5).
  bool biscuitReport   = false;        // While solo wardriving, also announce + forward found networks over ESP-NOW (ch 6) so a Biscuit Manager sees this device as a biscuit.
  String board = "auto"; // auto | s3 | c5 | c6 | c3 | exp  (pins selected at boot; reboot required after change)
  String speedUnits  = "kmh"; // kmh | mph
  int battPin        = 9;     // GPIO for battery voltage ADC (-1 = disabled). Expects 1:2 voltage divider from LiPo (FNK0104B: GPIO 9).
  bool batteryTest   = false; // Enable battery test (logs elapsed time to /battery_test.csv)
  
  // Boot auto-upload limit:
  //  -1 = upload ALL files at boot (no limit)
  //   0 = disabled (no auto-upload at boot)
  //  1+ = upload up to N files at boot (WiGLE allows 25 API calls/day)
  // IMPORTANT: Requires PSRAM enabled in Arduino IDE for reliable TLS connections.
  int maxBootUploads = 25;

  // WDGoWars API key from https://wdgwars.pl/profile -> "Generate API key".
  // If set, CSVs are uploaded to WDGoWars BEFORE WiGLE at every boot.
  // Leave empty to disable WDGoWars uploads.
  String wdgwarsApiKey;

  // Custom SSID broadcast by the Marauder beacon-spam tool when its SSID source
  // is set to "Custom". Empty falls back to a generated name.
  String beaconSsid;

  // Optional device name — appended to WiGLE CSV header and filename so
  // multiple Piglets uploading to the same account can be distinguished.
  // E.g. deviceName=rover1  →  device=Piglet-rover1  /  rover1_Piglet_WiGLE_....csv
  // Leave empty for default ("Piglet-Wardriver" / "WiGLE_....csv").
  String deviceName;

  // Auto-start mesh mode on boot: core, node, or none.
  // core — become the mesh coordinator (receives wardriving records from nodes).
  // node — become a scanning node that forwards records to the Core.
  // none — normal solo wardriving mode.
  // Default is "core" for this Core firmware build; change on the SYS screen.
  String meshModeOnBoot = "core";

  // Rotate the OLED display 180° (true = upside-down mount, false = normal).
  // Requires reboot to take effect.
  bool rotateScreen180 = false;

  // When true: after boot uploads complete, disconnect from home WiFi and
  // begin wardriving immediately instead of staying on the STA connection.
  // The web UI is still reachable if you connect to the Wardriver AP later,
  // but the device will not hold the STA link open. Requires reboot.
  bool autoStartAfterUpload = false;
};

const PinMap& detectPinsByChip();
PinMap pickPinsFromConfig();
bool wardriverIsC5();

String trimCopy(String s);
bool parseKeyValueLine(const String& lineIn, String& keyOut, String& valOut);
void cfgAssignKV(const String& k, const String& v);
bool loadConfigFromSD();
bool saveConfigToSD();
