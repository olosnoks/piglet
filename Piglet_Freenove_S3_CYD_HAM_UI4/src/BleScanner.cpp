#include "BleScanner.h"

#include <NimBLEDevice.h>
#include <math.h>
#include <unordered_map>

#include "Globals.h"
#include "GPS.h"
#include "Scanner.h"      // cached GPS position (lastLat/lastLon/...)
#include "SDUtils.h"
#include "MeshNode.h"     // meshCoreActive / meshNodeActive
#include "BleDetectors.h" // bleDetectorsActive() — yield when a detector owns BLE

namespace {

// ---- Geo-dedup: re-log a MAC only after it moves or goes stale ----
constexpr float    REFRESH_MIN_MOVE_M = 30.0f;
constexpr uint32_t REFRESH_TTL_MS     = 5UL * 60UL * 1000UL;
constexpr size_t   DEDUP_MAX_ENTRIES  = 40000;  // safety cap (~1 MB in PSRAM)

// BLE scan window: passive, ~50% radio duty so WiFi keeps time.
// NimBLE 2.x takes these directly in milliseconds.
constexpr uint16_t SCAN_INTERVAL_MS = 100;
constexpr uint16_t SCAN_WINDOW_MS   = 50;

// Cross-task handoff from the NimBLE host callback to the main loop.
struct BleObs {
  uint8_t mac[6];
  int8_t  rssi;
  char    name[32];
};
QueueHandle_t obsQueue = nullptr;

// PSRAM-backed allocator so the dedup table doesn't starve internal RAM.
template <class T>
struct PsramAllocator {
  using value_type = T;
  PsramAllocator() = default;
  template <class U> PsramAllocator(const PsramAllocator<U>&) {}
  T* allocate(size_t n) {
    void* p = heap_caps_malloc(n * sizeof(T), MALLOC_CAP_SPIRAM);
    if (!p) p = malloc(n * sizeof(T));  // fall back if no PSRAM free
    if (!p) throw std::bad_alloc();
    return static_cast<T*>(p);
  }
  void deallocate(T* p, size_t) { free(p); }
  template <class U> bool operator==(const PsramAllocator<U>&) const { return true; }
  template <class U> bool operator!=(const PsramAllocator<U>&) const { return false; }
};

struct Sighting {
  float    lat;
  float    lon;
  uint32_t ms;
};
using DedupMap = std::unordered_map<uint64_t, Sighting, std::hash<uint64_t>,
                                    std::equal_to<uint64_t>,
                                    PsramAllocator<std::pair<const uint64_t, Sighting>>>;
DedupMap* seen = nullptr;

bool inited = false;
bool scanning = false;
uint32_t uniqueCount = 0;

uint64_t macToKey(const uint8_t* m) {
  uint64_t k = 0;
  for (int i = 0; i < 6; ++i) k = (k << 8) | m[i];
  return k;
}

float distanceM(float lat1, float lon1, float lat2, float lon2) {
  constexpr float M_PER_DEG = 111320.0f;
  const float dy = (lat2 - lat1) * M_PER_DEG;
  const float dx = (lon2 - lon1) * M_PER_DEG * cosf(lat1 * 0.0174532925f);
  return sqrtf(dx * dx + dy * dy);
}

class ScanCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* d) override {
    if (!obsQueue) return;
    BleObs obs;
    // Parse the display-order MAC string ("aa:bb:cc:dd:ee:ff") into bytes so we
    // don't depend on the native byte order.
    unsigned int b[6] = {0};
    if (sscanf(d->getAddress().toString().c_str(), "%x:%x:%x:%x:%x:%x",
               &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6) {
      return;
    }
    for (int i = 0; i < 6; ++i) obs.mac[i] = static_cast<uint8_t>(b[i]);
    obs.rssi = static_cast<int8_t>(d->getRSSI());
    if (d->haveName()) {
      strncpy(obs.name, d->getName().c_str(), sizeof(obs.name) - 1);
      obs.name[sizeof(obs.name) - 1] = '\0';
    } else {
      obs.name[0] = '\0';
    }
    xQueueSend(obsQueue, &obs, 0);  // drop if full — next advert refills it
  }
};
ScanCallbacks scanCallbacks;

bool wantScan() {
  return cfg.bluetoothScan && scanningEnabled && !meshCoreActive &&
         !meshNodeActive && !bleDetectorsActive();
}

bool currentPosition(double& lat, double& lon, double& altM, double& accM) {
  if (gpsHasFix) {
    lat = gps.location.lat();
    lon = gps.location.lng();
    altM = gps.altitude.isValid() ? gps.altitude.meters() : 0.0;
    accM = gps.hdop.isValid() ? gps.hdop.hdop() : 0.0;
    return true;
  }
  if (lastGpsValid && (millis() - lastGpsValidMs) <= GPS_CACHE_MAX_MS) {
    lat = lastLat; lon = lastLon; altM = lastAlt; accM = lastAcc;
    return true;
  }
  return false;  // no position — skip logging (WDGoWars requires GPS)
}

void logObs(const BleObs& obs) {
  double lat, lon, altM, accM;
  if (!currentPosition(lat, lon, altM, accM)) return;

  const uint64_t key = macToKey(obs.mac);
  const uint32_t now = millis();

  auto it = seen->find(key);
  if (it != seen->end()) {
    const bool stale = (now - it->second.ms) >= REFRESH_TTL_MS;
    const bool moved = distanceM(it->second.lat, it->second.lon,
                                 static_cast<float>(lat), static_cast<float>(lon)) >= REFRESH_MIN_MOVE_M;
    if (!stale && !moved) return;  // same spot, seen recently — skip
    it->second = {static_cast<float>(lat), static_cast<float>(lon), now};
  } else {
    if (seen->size() < DEDUP_MAX_ENTRIES) {
      (*seen)[key] = {static_cast<float>(lat), static_cast<float>(lon), now};
    }
    uniqueCount++;
  }

  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
           obs.mac[0], obs.mac[1], obs.mac[2], obs.mac[3], obs.mac[4], obs.mac[5]);

  appendWigleBleRow(String(macStr), String(obs.name), obs.rssi,
                    iso8601NowUTC(), lat, lon, altM, accM);
  devicesFoundBle++;
}

void startScan() {
  NimBLEScan* s = NimBLEDevice::getScan();
  s->setScanCallbacks(&scanCallbacks, /*wantDuplicates*/ false);
  s->setActiveScan(false);  // passive: receive-only, no scan requests
  s->setInterval(SCAN_INTERVAL_MS);
  s->setWindow(SCAN_WINDOW_MS);
  s->setMaxResults(0);      // don't retain results internally; we use the callback
  s->start(0, /*isContinue*/ false, /*restart*/ true);  // 0 = scan until stopped, non-blocking
  scanning = true;
  Serial.println("[BLE] Scan started");
}

void stopScan() {
  NimBLEDevice::getScan()->stop();
  scanning = false;
  Serial.println("[BLE] Scan stopped");
}

}  // namespace

void bleScannerBegin() {
  obsQueue = xQueueCreate(64, sizeof(BleObs));
  seen = new DedupMap();
  Serial.println("[BLE] Scanner ready (lazy init on first enable)");
}

void bleScannerTick() {
  // A passive detector (AirTag/Flipper/Pwnagotchi) owns the NimBLE scan and its
  // own callback while active — don't touch the scan here or we'd stop it or
  // clobber its callback. The detector releases the radio on stop and we resume.
  if (bleDetectorsActive()) return;

  const bool want = wantScan();

  if (want && !inited) {
    NimBLEDevice::init("");
    inited = true;
    Serial.println("[BLE] NimBLE initialized");
  }

  if (inited) {
    if (want && !scanning) startScan();
    else if (!want && scanning) stopScan();

    // Watchdog: a forever-scan shouldn't end on its own, but restart if it did.
    if (want && scanning && !NimBLEDevice::getScan()->isScanning()) {
      NimBLEDevice::getScan()->start(0, false, true);
    }
  }

  // Drain a few observations per tick. Kept small so a burst of BLE SD writes
  // can't stall the loop long enough to overflow the GPS UART buffer. The queue
  // holds the backlog; excess is dropped at enqueue time, which is fine for
  // wardriving (the device will re-advertise).
  if (obsQueue) {
    BleObs obs;
    int budget = 8;
    while (budget-- > 0 && xQueueReceive(obsQueue, &obs, 0) == pdTRUE) {
      if (want) logObs(obs);
    }
  }
}

void bleScannerShutdown() {
  // Stop the active scan so BLE stops using the radio. We deliberately do NOT
  // NimBLEDevice::deinit() here: tearing down the BT controller during the
  // WiFi/ESP-NOW transition in enterCoreMode/enterNodeMode crashes the device.
  // An idle (non-scanning) controller has negligible impact on ESP-NOW.
  if (scanning) {
    NimBLEDevice::getScan()->stop();
    scanning = false;
    Serial.println("[BLE] Scan stopped for mesh");
  }
}

bool bleScannerActive() { return scanning; }
uint32_t bleUniqueCount() { return uniqueCount; }
