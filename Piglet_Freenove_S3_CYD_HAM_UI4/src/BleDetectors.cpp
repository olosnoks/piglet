#include "BleDetectors.h"
#include "AirTagMonitor.h"
#include "FlipperSniff.h"
#include "PwnagotchiDetect.h"

#include <NimBLEDevice.h>
#include <unordered_map>
#include <string>
#include <ctype.h>

#include "Globals.h"
#include "GPS.h"
#include "Scanner.h"        // cached GPS position (lastLat/lastLon/...)
#include "SDUtils.h"
#include "AppleBleSpam.h"
#include "BleScanner.h"     // bleScannerShutdown()
#include "MeshNode.h"       // meshCoreActive / meshNodeActive

namespace {

enum class Detector : uint8_t { None, AirTag, Flipper, Pwnagotchi };

Detector s_active = Detector::None;
bool     s_inited = false;

// Per-detector unique counts (persist across a session so the UI can show a
// running total even after a detector is stopped and restarted).
uint32_t s_countAirTag = 0;
uint32_t s_countFlipper = 0;
uint32_t s_countPwn = 0;

File     s_logFile;

// Re-log a MAC only after this gap so the CSV isn't spammed by a stationary
// device re-advertising. AirTags get a short 60 s gap (persistence tracking);
// Flipper/Pwnagotchi use a 5 min refresh window.
constexpr uint32_t GAP_AIRTAG_MS = 60UL * 1000UL;
constexpr uint32_t GAP_OTHER_MS  = 5UL * 60UL * 1000UL;

// Cross-task handoff from the NimBLE host callback to the main loop, mirroring
// BleScanner's pattern (SD writes happen on the main loop, not in the callback).
struct DetObs {
  uint8_t mac[6];
  int8_t  rssi;
  char    name[32];   // local name, or a hex id for AirTag
};
QueueHandle_t s_queue = nullptr;

// Dedup by MAC -> last-logged millis. Cleared on each start.
std::unordered_map<uint64_t, uint32_t> s_seen;

uint64_t macToKey(const uint8_t* m) {
  uint64_t k = 0;
  for (int i = 0; i < 6; ++i) k = (k << 8) | m[i];
  return k;
}

bool parseMac(const char* s, uint8_t out[6]) {
  unsigned int b[6] = {0};
  if (sscanf(s, "%x:%x:%x:%x:%x:%x", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6) {
    return false;
  }
  for (int i = 0; i < 6; ++i) out[i] = static_cast<uint8_t>(b[i]);
  return true;
}

const char* csvName(Detector d) {
  switch (d) {
    case Detector::AirTag:     return "airtags";
    case Detector::Flipper:    return "flippers";
    case Detector::Pwnagotchi: return "pwnagotchi";
    default:                   return "ble";
  }
}

const char* csvHeader(Detector d) {
  // AirTag logs an id column; the name-based detectors log the local name.
  if (d == Detector::AirTag) return "timestamp,mac,id,rssi,lat,lon\n";
  return "timestamp,mac,name,rssi,lat,lon\n";
}

// Position: live fix, else the quality-gated cache. Detectors log presence even
// without GPS (blank lat/lon), unlike the wardriving scanner.
bool currentPosition(double& lat, double& lon) {
  if (gpsHasFix) {
    lat = gps.location.lat();
    lon = gps.location.lng();
    return true;
  }
  if (lastGpsValid && (millis() - lastGpsValidMs) <= GPS_CACHE_MAX_MS) {
    lat = lastLat; lon = lastLon;
    return true;
  }
  return false;
}

void openLog(Detector d) {
  if (!sdOk) return;
  if (!SD.exists("/logs")) SD.mkdir("/logs");
  char path[64];
  snprintf(path, sizeof(path), "/logs/%s_%lu.csv", csvName(d), (unsigned long)millis());
  s_logFile = SD.open(path, FILE_WRITE);
  if (s_logFile) {
    s_logFile.print(csvHeader(d));
    s_logFile.flush();
    Serial.printf("[Detect] Logging to %s\n", path);
  } else {
    Serial.printf("[Detect] log open failed: %s\n", path);
  }
}

void logObs(const DetObs& obs) {
  const uint64_t key = macToKey(obs.mac);
  const uint32_t now = millis();
  const uint32_t gap = (s_active == Detector::AirTag) ? GAP_AIRTAG_MS : GAP_OTHER_MS;

  auto it = s_seen.find(key);
  bool isNew = false;
  if (it != s_seen.end()) {
    if (now - it->second < gap) return;   // seen recently — skip
    it->second = now;
  } else {
    if (s_seen.size() < 4000) s_seen[key] = now;
    isNew = true;
    switch (s_active) {
      case Detector::AirTag:     s_countAirTag++;  break;
      case Detector::Flipper:    s_countFlipper++; break;
      case Detector::Pwnagotchi: s_countPwn++;     break;
      default: break;
    }
  }
  (void)isNew;

  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
           obs.mac[0], obs.mac[1], obs.mac[2], obs.mac[3], obs.mac[4], obs.mac[5]);

  double lat = 0, lon = 0;
  const bool havePos = currentPosition(lat, lon);

  if (s_logFile) {
    s_logFile.print(iso8601NowUTC());
    s_logFile.print(',');
    s_logFile.print(macStr);
    s_logFile.print(',');
    s_logFile.print(obs.name);   // id (AirTag) or local name
    s_logFile.print(',');
    s_logFile.print(obs.rssi);
    s_logFile.print(',');
    if (havePos) { s_logFile.print(lat, 6); s_logFile.print(','); s_logFile.print(lon, 6); }
    else         { s_logFile.print(','); }
    s_logFile.print('\n');
    s_logFile.flush();
  }
}

// ---- Advertisement matching (runs in the NimBLE host callback) --------------

// AirTag / Find My: manufacturer data = 4C 00 (Apple, LE) then message type
// 0x12 (Find My). Fills `idOut` with the first payload bytes as hex.
bool matchAirTag(const std::string& md, char* idOut, size_t idCap) {
  if (md.size() < 4) return false;
  const uint8_t* p = reinterpret_cast<const uint8_t*>(md.data());
  if (p[0] != 0x4C || p[1] != 0x00) return false;   // not Apple
  if (p[2] != 0x12) return false;                    // not Find My
  // Rotating id: a few payload bytes after the header for the log.
  size_t n = md.size() > 8 ? 6 : (md.size() - 3);
  size_t w = 0;
  for (size_t i = 0; i < n && w + 2 < idCap; ++i) {
    w += snprintf(idOut + w, idCap - w, "%02X", p[3 + i]);
  }
  idOut[w] = '\0';
  return true;
}

bool nameContains(const std::string& name, const char* needleLower) {
  if (name.empty()) return false;
  std::string n = name;
  for (auto& c : n) c = (char)tolower((unsigned char)c);
  return n.find(needleLower) != std::string::npos;
}

bool matchFlipper(const std::string& name, const std::string& md) {
  if (nameContains(name, "flipper")) return true;
  if (md.size() >= 2) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(md.data());
    if (p[0] == 0x81 && p[1] == 0x30) return true;
  }
  return false;
}

bool matchPwnagotchi(const std::string& name) {
  return nameContains(name, "pwnagotchi") || nameContains(name, "pwngrid");
}

class DetCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* d) override {
    if (!s_queue) return;

    const std::string name = d->haveName() ? d->getName() : std::string();
    const std::string md   = d->haveManufacturerData() ? d->getManufacturerData() : std::string();

    DetObs obs;
    obs.name[0] = '\0';

    switch (s_active) {
      case Detector::AirTag:
        if (!matchAirTag(md, obs.name, sizeof(obs.name))) return;
        break;
      case Detector::Flipper:
        if (!matchFlipper(name, md)) return;
        strncpy(obs.name, name.c_str(), sizeof(obs.name) - 1);
        obs.name[sizeof(obs.name) - 1] = '\0';
        break;
      case Detector::Pwnagotchi:
        if (!matchPwnagotchi(name)) return;
        strncpy(obs.name, name.c_str(), sizeof(obs.name) - 1);
        obs.name[sizeof(obs.name) - 1] = '\0';
        break;
      default:
        return;
    }

    if (!parseMac(d->getAddress().toString().c_str(), obs.mac)) return;
    obs.rssi = static_cast<int8_t>(d->getRSSI());
    xQueueSend(s_queue, &obs, 0);   // drop if full — the device re-advertises
  }
};
DetCallbacks s_cb;

void ensureQueue() {
  if (!s_queue) s_queue = xQueueCreate(48, sizeof(DetObs));
}

bool startDetector(Detector d, bool activeScan) {
  if (s_active == d) return true;

  if (meshCoreActive || meshNodeActive) {
    Serial.println("[Detect] refused: mesh mode owns the radio");
    return false;
  }
  if (appleBleSpamActive()) {
    Serial.println("[Detect] refused: BLE spam is active");
    return false;
  }

  // Yield the wardriving BLE scanner and take over the scan callback.
  bleScannerShutdown();
  ensureQueue();

  if (!NimBLEDevice::isInitialized()) {
    NimBLEDevice::init("");
  }
  s_inited = true;

  // Fresh session state for the new detector.
  s_seen.clear();
  if (s_logFile) { s_logFile.flush(); s_logFile.close(); }

  s_active = d;
  openLog(d);

  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setScanCallbacks(&s_cb, /*wantDuplicates*/ true);
  scan->setActiveScan(activeScan);   // active: also solicit scan-response names
  scan->setInterval(100);
  scan->setWindow(50);
  scan->setMaxResults(0);
  scan->start(0, /*isContinue*/ false, /*restart*/ true);

  Serial.printf("[Detect] started %s (%s scan)\n",
                csvName(d), activeScan ? "active" : "passive");
  return true;
}

void stopDetector() {
  if (s_active == Detector::None) return;
  Serial.printf("[Detect] stopped %s\n", csvName(s_active));
  if (s_inited) NimBLEDevice::getScan()->stop();
  if (s_logFile) { s_logFile.flush(); s_logFile.close(); }
  s_active = Detector::None;
  // Leave counts intact; the wardriving BLE scanner resumes on the next tick.
}

// Shared per-loop service: drain matched observations and log them. Called by
// each detector's Tick (idempotent — draining an empty queue is nearly free).
void serviceTick() {
  if (s_active == Detector::None) return;

  // Watchdog: a forever-scan shouldn't end on its own, but restart if it did.
  if (s_inited && !NimBLEDevice::getScan()->isScanning()) {
    NimBLEDevice::getScan()->start(0, false, true);
  }

  if (!s_queue) return;
  DetObs obs;
  int budget = 8;
  while (budget-- > 0 && xQueueReceive(s_queue, &obs, 0) == pdTRUE) {
    logObs(obs);
  }
}

}  // namespace

// ---- Shared coordination -----------------------------------------------------

bool bleDetectorsActive() { return s_active != Detector::None; }

// ---- AirTag Monitor ----------------------------------------------------------

void     airTagMonitorBegin()  { ensureQueue(); }
void     airTagMonitorStart()  { startDetector(Detector::AirTag, /*active*/ false); }
void     airTagMonitorStop()   { if (s_active == Detector::AirTag) stopDetector(); }
bool     airTagMonitorActive() { return s_active == Detector::AirTag; }
uint32_t airTagMonitorCount()  { return s_countAirTag; }
void     airTagMonitorTick()   { serviceTick(); }

// ---- Flipper Sniff -----------------------------------------------------------

void     flipperSniffBegin()  { ensureQueue(); }
void     flipperSniffStart()  { startDetector(Detector::Flipper, /*active*/ true); }
void     flipperSniffStop()   { if (s_active == Detector::Flipper) stopDetector(); }
bool     flipperSniffActive() { return s_active == Detector::Flipper; }
uint32_t flipperSniffCount()  { return s_countFlipper; }
void     flipperSniffTick()   { serviceTick(); }

// ---- Pwnagotchi Detect -------------------------------------------------------

void     pwnagotchiDetectBegin()  { ensureQueue(); }
void     pwnagotchiDetectStart()  { startDetector(Detector::Pwnagotchi, /*active*/ true); }
void     pwnagotchiDetectStop()   { if (s_active == Detector::Pwnagotchi) stopDetector(); }
bool     pwnagotchiDetectActive() { return s_active == Detector::Pwnagotchi; }
uint32_t pwnagotchiDetectCount()  { return s_countPwn; }
void     pwnagotchiDetectTick()   { serviceTick(); }
