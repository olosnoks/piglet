#include "RawCapture.h"

#include "Globals.h"
#include "SDUtils.h"
#include "MarauderTools.h"

// 802.11 management subtype counters (index = frame subtype). EAPOL is detected
// separately in data frames. Marked volatile because they are written from the
// WiFi RX callback and read from the main loop.
namespace {

// Management subtypes we track (subtype = (frame_control[0] >> 4) & 0x0F).
volatile uint32_t s_beacon    = 0;   // 0x08
volatile uint32_t s_probeReq  = 0;   // 0x04
volatile uint32_t s_probeResp = 0;   // 0x05
volatile uint32_t s_auth      = 0;   // 0x0B
volatile uint32_t s_assocReq  = 0;   // 0x00
volatile uint32_t s_assocResp = 0;   // 0x01
volatile uint32_t s_deauth    = 0;   // 0x0C
volatile uint32_t s_disassoc  = 0;   // 0x0A
volatile uint32_t s_eapol     = 0;   // data frames carrying EtherType 0x888E
volatile uint32_t s_other     = 0;
volatile uint32_t s_total     = 0;

RawCaptureFilter s_filter = RawCaptureFilter::AllMgmt;

void resetCounters() {
  s_beacon = s_probeReq = s_probeResp = s_auth = 0;
  s_assocReq = s_assocResp = s_deauth = s_disassoc = 0;
  s_eapol = s_other = s_total = 0;
}

bool filterAllowsMgmt(uint8_t subtype) {
  switch (s_filter) {
    case RawCaptureFilter::AllMgmt: return true;
    case RawCaptureFilter::Beacons: return subtype == 0x08;
    case RawCaptureFilter::Probes:  return subtype == 0x04 || subtype == 0x05;
    case RawCaptureFilter::Deauth:  return subtype == 0x0C || subtype == 0x0A;
    case RawCaptureFilter::Eapol:   return false;   // EAPOL is data-only
  }
  return true;
}

}  // namespace

void rawCaptureBegin() { resetCounters(); }

bool rawCaptureWantsData() { return s_filter == RawCaptureFilter::Eapol; }

RawCaptureFilter rawCaptureCurrentFilter() { return s_filter; }

void IRAM_ATTR rawCaptureOnFrame(const uint8_t* frame, uint16_t len, bool isData) {
  if (len < 1) return;

  if (isData) {
    // EAPOL rides in data frames. The LLC/SNAP EtherType 0x88 0x8E sits a fixed
    // offset into the payload; scan a small window rather than parse the header
    // (which varies with QoS / addr4 / protection).
    if (s_filter == RawCaptureFilter::Eapol || s_filter == RawCaptureFilter::AllMgmt) {
      const int scanEnd = (len > 64) ? 64 : (len - 1);
      for (int i = 24; i < scanEnd; ++i) {
        if (frame[i] == 0x88 && frame[i + 1] == 0x8E) { s_eapol++; s_total++; break; }
      }
    }
    return;
  }

  const uint8_t type    = (frame[0] >> 2) & 0x03;
  const uint8_t subtype = (frame[0] >> 4) & 0x0F;
  if (type != 0x00) { return; }   // only management frames counted here

  if (!filterAllowsMgmt(subtype)) return;

  switch (subtype) {
    case 0x08: s_beacon++;    break;
    case 0x04: s_probeReq++;  break;
    case 0x05: s_probeResp++; break;
    case 0x0B: s_auth++;      break;
    case 0x00: s_assocReq++;  break;
    case 0x01: s_assocResp++; break;
    case 0x0C: s_deauth++;    break;
    case 0x0A: s_disassoc++;  break;
    default:   s_other++;     break;
  }
  s_total++;
}

void rawCaptureStart(RawCaptureFilter filter, uint8_t channel) {
  s_filter = filter;
  resetCounters();
  marauderStartAttack(AttackMode::RawCapture, channel);
}

void rawCaptureStop() {
  const bool wasActive = rawCaptureActive();
  marauderStopAttack();

  // One-line summary on stop so a capture leaves a record without per-frame SD load.
  if (wasActive && sdOk) {
    if (!SD.exists("/logs")) SD.mkdir("/logs");
    char path[64];
    snprintf(path, sizeof(path), "/logs/raw_%lu.csv", (unsigned long)millis());
    File f = SD.open(path, FILE_WRITE);
    if (f) {
      f.println("type,count");
      f.printf("beacon,%lu\n",     (unsigned long)s_beacon);
      f.printf("probe_req,%lu\n",  (unsigned long)s_probeReq);
      f.printf("probe_resp,%lu\n", (unsigned long)s_probeResp);
      f.printf("auth,%lu\n",       (unsigned long)s_auth);
      f.printf("assoc_req,%lu\n",  (unsigned long)s_assocReq);
      f.printf("assoc_resp,%lu\n", (unsigned long)s_assocResp);
      f.printf("deauth,%lu\n",     (unsigned long)s_deauth);
      f.printf("disassoc,%lu\n",   (unsigned long)s_disassoc);
      f.printf("eapol,%lu\n",      (unsigned long)s_eapol);
      f.printf("other,%lu\n",      (unsigned long)s_other);
      f.printf("total,%lu\n",      (unsigned long)s_total);
      f.flush();
      f.close();
      Serial.printf("[Raw] summary -> %s\n", path);
    }
  }
}

bool rawCaptureActive() { return marauderCurrentAttack() == AttackMode::RawCapture; }

uint32_t rawCaptureFrames() { return s_total; }

void rawCaptureTick() { /* counting happens in the RX callback; nothing to do */ }

String rawCaptureBreakdown() {
  // Compact form for the small display. Only non-zero groups are shown.
  String out;
  auto add = [&](const char* k, uint32_t v) {
    if (v == 0) return;
    if (out.length()) out += ' ';
    out += k; out += ':'; out += String(v);
  };
  add("B", s_beacon);
  add("P", s_probeReq + s_probeResp);
  add("A", s_auth);
  add("D", s_deauth + s_disassoc);
  add("E", s_eapol);
  add("O", s_other);
  if (out.length() == 0) out = "(none)";
  return out;
}
