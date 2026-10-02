#include "WpsCheck.h"

#include "Globals.h"
#include "SDUtils.h"
#include "MarauderTools.h"
#include "MeshNode.h"

#include <WiFi.h>
#include <esp_wifi.h>
#include <vector>

namespace {

struct WpsAp {
  String  ssid;
  String  bssid;
  int     ch;
  int     rssi;
  bool    wps;
};

std::vector<WpsAp> s_aps;
size_t s_enabled = 0;

}  // namespace

int wpsScanRun() {
  s_aps.clear();
  s_enabled = 0;

  if (marauderAttackActive() || meshCoreActive || meshNodeActive) {
    Serial.println("[WPS] refused: radio owned by attack/mesh");
    return -1;
  }

  // Pause the wardriving scan and let any in-flight Arduino async scan settle so
  // our direct esp_wifi_scan_start() doesn't collide with it.
  const bool prevScanEnabled = scanningEnabled;
  scanningEnabled = false;

  const wifi_mode_t m = WiFi.getMode();
  if (m != WIFI_STA && m != WIFI_AP_STA) {
    WiFi.mode(WIFI_STA);
    delay(80);
  }

  const uint32_t t0 = millis();
  while (WiFi.scanComplete() == WIFI_SCAN_RUNNING && millis() - t0 < 4000) {
    delay(50);
  }
  WiFi.scanDelete();

  wifi_scan_config_t sc = {};
  sc.show_hidden = true;
  esp_err_t e = esp_wifi_scan_start(&sc, /*block*/ true);
  if (e != ESP_OK) {
    Serial.printf("[WPS] scan start err=0x%x (%s)\n", e, esp_err_to_name(e));
    scanningEnabled = prevScanEnabled;
    return -1;
  }

  uint16_t num = 0;
  esp_wifi_scan_get_ap_num(&num);
  if (num == 0) {
    scanningEnabled = prevScanEnabled;
    Serial.println("[WPS] 0 APs");
    return 0;
  }
  if (num > 64) num = 64;

  wifi_ap_record_t* recs =
      static_cast<wifi_ap_record_t*>(malloc(sizeof(wifi_ap_record_t) * num));
  if (!recs) {
    scanningEnabled = prevScanEnabled;
    return -1;
  }

  if (esp_wifi_scan_get_ap_records(&num, recs) != ESP_OK) {
    free(recs);
    scanningEnabled = prevScanEnabled;
    return -1;
  }

  s_aps.reserve(num);
  for (int i = 0; i < num; ++i) {
    WpsAp a;
    a.ssid = String(reinterpret_cast<const char*>(recs[i].ssid));
    char b[18];
    snprintf(b, sizeof(b), "%02X:%02X:%02X:%02X:%02X:%02X",
             recs[i].bssid[0], recs[i].bssid[1], recs[i].bssid[2],
             recs[i].bssid[3], recs[i].bssid[4], recs[i].bssid[5]);
    a.bssid = String(b);
    a.ch   = recs[i].primary;
    a.rssi = recs[i].rssi;
    a.wps  = recs[i].wps;
    if (a.wps) s_enabled++;
    s_aps.push_back(a);
  }
  free(recs);

  scanningEnabled = prevScanEnabled;
  Serial.printf("[WPS] %u APs, %u with WPS\n",
                (unsigned)s_aps.size(), (unsigned)s_enabled);
  return static_cast<int>(s_aps.size());
}

size_t wpsApCount()      { return s_aps.size(); }
size_t wpsEnabledCount() { return s_enabled; }

bool wpsGet(size_t index, String& ssid, String& bssid, int& channel, int& rssi, bool& wps) {
  if (index >= s_aps.size()) return false;
  ssid    = s_aps[index].ssid;
  bssid   = s_aps[index].bssid;
  channel = s_aps[index].ch;
  rssi    = s_aps[index].rssi;
  wps     = s_aps[index].wps;
  return true;
}

bool wpsCheckAp(int index) {
  if (index < 0 || static_cast<size_t>(index) >= s_aps.size()) return false;
  return s_aps[index].wps;
}

const char* wpsLabel(int index) {
  if (index < 0 || static_cast<size_t>(index) >= s_aps.size()) return "-";
  return s_aps[index].wps ? "WPS" : "-";
}

bool wpsLogResults() {
  if (!sdOk || s_aps.empty()) return false;
  if (!SD.exists("/logs")) SD.mkdir("/logs");
  char path[64];
  snprintf(path, sizeof(path), "/logs/wps_%lu.csv", (unsigned long)millis());
  File f = SD.open(path, FILE_WRITE);
  if (!f) {
    Serial.printf("[WPS] log open failed: %s\n", path);
    return false;
  }
  f.println("bssid,ssid,wps");
  for (const auto& a : s_aps) {
    f.print(a.bssid);
    f.print(',');
    f.print(a.ssid);
    f.print(',');
    f.println(a.wps ? "1" : "0");
  }
  f.flush();
  f.close();
  Serial.printf("[WPS] logged %u rows -> %s\n", (unsigned)s_aps.size(), path);
  return true;
}
