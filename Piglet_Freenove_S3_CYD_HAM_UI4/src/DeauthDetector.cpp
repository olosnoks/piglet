// DeauthDetector — passive monitor for 802.11 deauth/disassoc frames.
//
// Receive-only defensive tooling: it detects when deauthentication attacks are
// happening nearby. It never transmits. (Running deauth attacks against networks
// you do not own or are not authorized to test is illegal in most jurisdictions;
// this module does the opposite — it flags such attacks.)

#include "DeauthDetector.h"

#include "Globals.h"
#include "SDUtils.h"
#include "GPS.h"        // iso8601NowUTC()
#include "MarauderTools.h"

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <string.h>

namespace {

// Cross-task handoff from the WiFi RX callback to the main loop (SD writes and
// the unique-source set are handled on the loop, not in the callback).
struct DeauthEvt {
  uint8_t  src[6];
  uint8_t  dst[6];
  uint8_t  subtype;   // 0x0C deauth, 0x0A disassoc
  int8_t   rssi;
  uint32_t ms;
};

QueueHandle_t s_queue = nullptr;

volatile uint32_t s_total = 0;   // written in the RX callback

File     s_logFile;

// Unique source MACs (packed into uint64 keys).
constexpr size_t MAX_SOURCES = 32;
uint64_t s_sources[MAX_SOURCES];
size_t   s_sourceCount = 0;

// Ring of recent event timestamps for the rolling-rate / alert calculation.
constexpr size_t RECENT_N = 160;
uint32_t s_recent[RECENT_N];
size_t   s_recentHead = 0;
constexpr uint32_t ALERT_WINDOW_MS = 10000;   // 10 s
constexpr uint32_t ALERT_THRESHOLD = 100;     // >100 frames / 10 s -> alert

uint64_t macKey(const uint8_t* m) {
  uint64_t k = 0;
  for (int i = 0; i < 6; ++i) k = (k << 8) | m[i];
  return k;
}

void noteSource(const uint8_t* mac) {
  const uint64_t k = macKey(mac);
  for (size_t i = 0; i < s_sourceCount; ++i) {
    if (s_sources[i] == k) return;
  }
  if (s_sourceCount < MAX_SOURCES) s_sources[s_sourceCount++] = k;
}

uint32_t recentCount() {
  const uint32_t now = millis();
  uint32_t n = 0;
  for (size_t i = 0; i < RECENT_N; ++i) {
    const uint32_t t = s_recent[i];
    if (t != 0 && (now - t) <= ALERT_WINDOW_MS) n++;
  }
  return n;
}

void openLog() {
  if (!sdOk) return;
  if (!SD.exists("/logs")) SD.mkdir("/logs");
  char path[64];
  snprintf(path, sizeof(path), "/logs/deauth_detect_%lu.csv", (unsigned long)millis());
  s_logFile = SD.open(path, FILE_WRITE);
  if (s_logFile) {
    s_logFile.print("timestamp,source,dest,subtype,rssi\n");
    s_logFile.flush();
    Serial.printf("[DeauthDet] logging to %s\n", path);
  } else {
    Serial.printf("[DeauthDet] log open failed: %s\n", path);
  }
}

}  // namespace

void deauthDetectorBegin() {
  if (!s_queue) s_queue = xQueueCreate(64, sizeof(DeauthEvt));
}

bool deauthDetectorStart(uint8_t channel) {
  if (!s_queue) deauthDetectorBegin();

  s_total = 0;
  s_sourceCount = 0;
  s_recentHead = 0;
  memset(s_recent, 0, sizeof(s_recent));
  if (s_queue) xQueueReset(s_queue);

  if (s_logFile) { s_logFile.flush(); s_logFile.close(); }
  openLog();

  // MarauderTools owns the radio: it locks the channel, puts the driver in RX-only
  // sniffer mode and installs the promiscuous filter that feeds deauthDetectorOnFrame.
  if (!marauderStartAttack(AttackMode::DeauthDetect, channel)) {
    if (s_logFile) { s_logFile.flush(); s_logFile.close(); }
    Serial.println("[DeauthDet] start refused by MarauderTools");
    return false;
  }
  Serial.printf("[DeauthDet] started on ch %u\n", channel);
  return true;
}

void deauthDetectorStop() {
  if (marauderCurrentAttack() == AttackMode::DeauthDetect) marauderStopAttack();
  if (s_logFile) { s_logFile.flush(); s_logFile.close(); }
  Serial.printf("[DeauthDet] stopped (%lu frames, %u sources)\n",
                (unsigned long)s_total, (unsigned)s_sourceCount);
}

bool     deauthDetectorActive()        { return marauderCurrentAttack() == AttackMode::DeauthDetect; }
uint32_t deauthDetectorCount()         { return s_total; }
uint32_t deauthDetectorUniqueSources() { return (uint32_t)s_sourceCount; }
bool     deauthDetectorAlert()         { return recentCount() > ALERT_THRESHOLD; }

String deauthDetectorStatusLine() {
  char buf[40];
  snprintf(buf, sizeof(buf), "src %u   10s %lu",
           (unsigned)s_sourceCount, (unsigned long)recentCount());
  return String(buf);
}

void IRAM_ATTR deauthDetectorOnFrame(const uint8_t* frame, uint16_t len, int8_t rssi) {
  if (len < 24) return;
  const uint8_t subtype = (frame[0] >> 4) & 0x0F;
  if (subtype != 0x0C && subtype != 0x0A) return;   // deauth / disassoc only

  s_total++;
  if (!s_queue) return;

  DeauthEvt e;
  memcpy(e.dst, frame + 4,  6);   // addr1 = receiver / dest
  memcpy(e.src, frame + 10, 6);   // addr2 = transmitter / source
  e.subtype = subtype;
  e.rssi    = rssi;
  e.ms      = millis();
  xQueueSend(s_queue, &e, 0);     // drop if full — counters already updated
}

void deauthDetectorTick() {
  if (!s_queue) return;

  DeauthEvt e;
  int budget = 16;
  while (budget-- > 0 && xQueueReceive(s_queue, &e, 0) == pdTRUE) {
    noteSource(e.src);

    s_recent[s_recentHead] = e.ms;
    s_recentHead = (s_recentHead + 1) % RECENT_N;

    if (s_logFile) {
      char src[18], dst[18];
      snprintf(src, sizeof(src), "%02X:%02X:%02X:%02X:%02X:%02X",
               e.src[0], e.src[1], e.src[2], e.src[3], e.src[4], e.src[5]);
      snprintf(dst, sizeof(dst), "%02X:%02X:%02X:%02X:%02X:%02X",
               e.dst[0], e.dst[1], e.dst[2], e.dst[3], e.dst[4], e.dst[5]);
      s_logFile.printf("%s,%s,%s,%s,%d\n",
                       iso8601NowUTC().c_str(), src, dst,
                       e.subtype == 0x0C ? "deauth" : "disassoc", e.rssi);
      s_logFile.flush();
    }
  }
}
