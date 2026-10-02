#include "HashcatWriter.h"

#include "Globals.h"
#include "SDUtils.h"

#include <string.h>
#include <stdio.h>

// ─────────────────────────────────────────────────────────────────────────────
//  EAPOL-Key frame layout reference (802.11-2016 §12.7.2)
//
//  802.11 header (24 bytes, or 26/30 with QoS/Addr4)
//  LLC/SNAP header (8 bytes): AA AA 03 00 00 00 88 8E
//  EAPOL header (4 bytes):
//     byte 0   : version (1..3)
//     byte 1   : type (3 = EAPOL-Key)
//     bytes 2-3: length (big-endian, length of body)
//  EAPOL-Key body (95+ bytes):
//     byte 0    : descriptor type (2 = RSN)
//     bytes 1-2 : key info (big-endian)
//                   bit  3 (0x0008) : MIC present
//                   bit  6 (0x0040) : secure
//                   bit  7 (0x0080) : error
//                   bit 12 (0x1000) : key ACK
//     bytes 3-4 : key length
//     bytes 5-12: replay counter (big-endian)
//     bytes 13-44: key nonce (32 bytes)
//     bytes 45-60: key IV (16 bytes)
//     bytes 61-68: key RSC (8 bytes)
//     bytes 69-76: key ID (8 bytes)
//     bytes 77-92: key MIC (16 bytes)
//     bytes 93-94: key data length (big-endian)
//     bytes 95+ : key data (contains PMKID KDE for M1 if PMF/PMKID is used)
//
//  PMKID KDE (in key data):
//     OUI 00 0F AC, type 04, length 10, then 16 bytes of PMKID
// ─────────────────────────────────────────────────────────────────────────────

namespace {

constexpr size_t MAX_SESSIONS = 16;

struct HandshakeSession {
  uint8_t  apMac[6];
  uint8_t  staMac[6];
  bool     valid;

  // M1 / M3 store the AP nonce (from the EAPOL-Key frame the AP sent).
  uint8_t  anonce[32];
  bool     haveAnonce;

  // M2 / M4 store the MIC and the raw EAPOL frame that came with it.
  uint8_t  mic[16];
  uint8_t  eapol[256];
  uint16_t eapolLen;
  bool     haveMic;

  // Which of the four messages we've seen (bit 0 = M1, bit 1 = M2, ...).
  uint8_t  seen;

  // Replay counter from the most recent frame, used to pair messages.
  uint8_t  replay[8];

  // PMKID captured from M1's key data (if present).
  uint8_t  pmkid[16];
  bool     havePmkid;

  uint32_t lastSeenMs;
  uint32_t dedupUntilMs;   // suppress re-emitting this session for 60 s
};

HandshakeSession s_sessions[MAX_SESSIONS] = {};

File     s_file;
String   s_path;
bool     s_open = false;

// Producer-consumer: completed lines are queued in the RX callback and
// flushed to SD from the main loop via hashcatWriterTick().
constexpr size_t OUT_QUEUE = 8;
String   s_outQueue[OUT_QUEUE];
volatile size_t s_outHead = 0;
volatile size_t s_outTail = 0;

uint32_t s_handshakeCount = 0;
uint32_t s_pmkidCount     = 0;

// ── Small helpers ──────────────────────────────────────────────────────────

inline uint16_t be16(const uint8_t* p) {
  return (uint16_t)((p[0] << 8) | p[1]);
}

inline uint64_t be64(const uint8_t* p) {
  uint64_t v = 0;
  for (int i = 0; i < 8; ++i) v = (v << 8) | p[i];
  return v;
}

void macNoSep(const uint8_t* mac, char* out) {
  // 12 lowercase hex chars, no separators.
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

// Find or allocate a session for this (AP, STA) pair. Returns nullptr if the
// table is full and no stale slot can be evicted.
HandshakeSession* findSession(const uint8_t* ap, const uint8_t* sta) {
  HandshakeSession* oldest = nullptr;
  for (size_t i = 0; i < MAX_SESSIONS; ++i) {
    HandshakeSession& s = s_sessions[i];
    if (s.valid && memcmp(s.apMac, ap, 6) == 0 && memcmp(s.staMac, sta, 6) == 0) {
      return &s;
    }
    if (!oldest || (s.valid && s.lastSeenMs < oldest->lastSeenMs)) {
      oldest = &s;
    }
  }
  // Evict oldest (including empty slots, since they have lastSeenMs == 0).
  HandshakeSession* s = oldest;
  memset(s, 0, sizeof(*s));
  memcpy(s->apMac, ap, 6);
  memcpy(s->staMac, sta, 6);
  s->valid = true;
  return s;
}

// Walk the 802.11 header to find the LLC/SNAP offset, then return a pointer
// to the EAPOL frame. Returns nullptr if this isn't an EAPOL data frame.
const uint8_t* findEapol(const uint8_t* frame, uint16_t len, uint16_t& eapolLen) {
  if (len < 24 + 8 + 4) return nullptr;

  const uint8_t fc0 = frame[0];
  const uint8_t type    = (fc0 >> 2) & 0x03;
  const uint8_t subtype = (fc0 >> 4) & 0x0F;
  if (type != 0x02) return nullptr;   // only data frames carry EAPOL
  // Subtypes 0x08..0x0F are QoS data; those have an extra 2-byte header.
  const uint8_t hdrLen = (subtype & 0x08) ? 26 : 24;

  // ToDS / FromDS determine address order; we don't need them for EAPOL
  // extraction, just to find the header length, which the QoS bit handles.

  if (len < hdrLen + 8) return nullptr;

  // LLC/SNAP: AA AA 03 00 00 00 88 8E
  const uint8_t* llc = frame + hdrLen;
  if (llc[0] != 0xAA || llc[1] != 0xAA || llc[2] != 0x03) return nullptr;
  if (llc[6] != 0x88 || llc[7] != 0x8E) return nullptr;

  const uint8_t* eapol = llc + 8;
  const uint16_t avail = (uint16_t)(len - hdrLen - 8);
  if (avail < 4) return nullptr;

  // EAPOL type 3 = EAPOL-Key.
  if (eapol[1] != 0x03) return nullptr;

  eapolLen = avail;
  return eapol;
}

// Extract a PMKID from the key data (if present). Returns true on success.
bool findPmkid(const uint8_t* eapol, uint16_t eapolLen, uint8_t pmkidOut[16]) {
  // Need at least the fixed EAPOL-Key body (99 bytes from EAPOL start).
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

    if (tagType == 0xDD && tagLen >= 4 + 16) {   // vendor-specific
      const uint8_t* v = kd + off + 2;
      if (v[0] == 0x00 && v[1] == 0x0F && v[2] == 0xAC && v[3] == 0x04) {
        memcpy(pmkidOut, v + 4, 16);
        return true;
      }
    }
    off += 2 + tagLen;
  }
  return false;
}

// Emit a .hc22000 line for a complete handshake. `msgPair` follows Hashcat:
//   0x00 = M1+M2, 0x02 = M2+M3, 0x04 = M3+M4.
String buildHandshakeLine(const HandshakeSession& s, uint8_t msgPair) {
  char apHex[13], staHex[13];
  macNoSep(s.apMac, apHex);
  macNoSep(s.staMac, staHex);

  String line;
  line.reserve(400);
  line += F("WPA*02*");
  hexEncode(s.mic, 16, line);
  line += '*'; line += apHex;
  line += '*'; line += staHex;
  line += '*';
  // ESSID unknown at this layer; leave it empty (Wireshark/hashcat will treat
  // it as an anonymous network — the handshake still cracks).
  line += '*';
  hexEncode(s.anonce, 32, line);
  line += '*';
  hexEncode(s.eapol, s.eapolLen, line);
  line += '*';
  char mp[3];
  snprintf(mp, sizeof(mp), "%02x", msgPair);
  line += mp;
  return line;
}

String buildPmkidLine(const HandshakeSession& s) {
  char apHex[13], staHex[13];
  macNoSep(s.apMac, apHex);
  macNoSep(s.staMac, staHex);

  String line;
  line.reserve(120);
  line += F("WPA*01*");
  hexEncode(s.pmkid, 16, line);
  line += '*'; line += apHex;
  line += '*'; line += staHex;
  line += "***";   // empty ESSID, no anonce/eapol/msgpair for PMKID
  return line;
}

void enqueueLine(const String& line) {
  const size_t next = (s_outHead + 1) % OUT_QUEUE;
  if (next == s_outTail) return;   // full — drop; rare in practice
  s_outQueue[s_outHead] = line;
  s_outHead = next;
}

void drainOutput() {
  if (!s_open || !s_file) return;
  while (s_outTail != s_outHead) {
    s_file.println(s_outQueue[s_outTail]);
    s_outQueue[s_outTail] = "";
    s_outTail = (s_outTail + 1) % OUT_QUEUE;
  }
  s_file.flush();
}

}  // namespace

// ── Public API ──────────────────────────────────────────────────────────────

void hashcatWriterBegin() {
  memset(s_sessions, 0, sizeof(s_sessions));
  s_outHead = s_outTail = 0;
  s_open = false;
  s_handshakeCount = 0;
  s_pmkidCount = 0;
}

bool hashcatWriterOpen(const String& ssidHint) {
  (void)ssidHint;
  if (!sdOk) return false;
  if (s_open) hashcatWriterClose();
  if (!SD.exists("/logs")) SD.mkdir("/logs");

  char buf[80];
  snprintf(buf, sizeof(buf), "/logs/hashcat_%lu.hc22000",
           (unsigned long)millis());
  s_path = String(buf);

  s_file = SD.open(s_path, FILE_WRITE);
  if (!s_file) {
    Serial.printf("[Hashcat] open failed: %s\n", s_path.c_str());
    s_path = "";
    return false;
  }
  s_file.flush();
  s_open = true;
  Serial.printf("[Hashcat] open: %s\n", s_path.c_str());
  return true;
}

void hashcatWriterClose() {
  if (!s_open) return;
  drainOutput();
  if (s_file) {
    s_file.flush();
    s_file.close();
  }
  Serial.printf("[Hashcat] closed %s (hs=%lu pmk=%lu)\n",
                s_path.c_str(),
                (unsigned long)s_handshakeCount,
                (unsigned long)s_pmkidCount);
  s_open = false;
  s_path = "";
}

bool     hashcatWriterIsOpen()             { return s_open; }
uint32_t hashcatWriterHandshakeCount()     { return s_handshakeCount; }
uint32_t hashcatWriterPmkidCount()         { return s_pmkidCount; }
String   hashcatWriterCurrentPath()        { return s_path; }

void IRAM_ATTR hashcatWriterObserve(const uint8_t* frame, uint16_t len) {
  if (!s_open) return;

  uint16_t eapolLen = 0;
  const uint8_t* eapol = findEapol(frame, len, eapolLen);
  if (!eapol) return;

  // EAPOL-Key body starts 4 bytes after the EAPOL header.
  const uint8_t* body = eapol + 4;
  if (eapolLen < 4 + 95) return;

  const uint16_t keyInfo = be16(body + 1);
  const bool micPresent  = (keyInfo & 0x0008) != 0;
  const bool secureBit   = (keyInfo & 0x0040) != 0;
  const bool ackBit      = (keyInfo & 0x1000) != 0;

  // 802.11 addresses: frame[4..9] = addr1, [10..15] = addr2, [16..21] = addr3.
  // For an AP→STA frame: addr2 (transmitter) = AP, addr1 = STA.
  // For an STA→AP frame: addr2 = STA, addr1 = AP.
  // ToDS / FromDS bits pick which is which.
  const uint8_t toDs   = (frame[1] >> 0) & 0x01;
  const uint8_t fromDs = (frame[1] >> 1) & 0x01;

  const uint8_t* addr1 = frame + 4;
  const uint8_t* addr2 = frame + 10;

  const uint8_t* apMac  = nullptr;
  const uint8_t* staMac = nullptr;
  if (fromDs && !toDs)       { apMac = addr2; staMac = addr1; }  // AP→STA
  else if (!fromDs && toDs)  { apMac = addr1; staMac = addr2; }  // STA→AP
  else                        return;   // IBSS or WDS — not a normal BSS handshake

  HandshakeSession* s = findSession(apMac, staMac);
  if (!s) return;

  s->lastSeenMs = millis();

  // Dedup: suppress re-emitting for 60 s after a successful emit.
  if (s->dedupUntilMs != 0 && millis() < s->dedupUntilMs) return;

  const uint32_t replayLo = (uint32_t)be64(body + 5);

  // Determine which handshake message this is by the key info bits and the
  // direction. Standard classification:
  //   M1: AP→STA, ACK=1, MIC=0
  //   M2: STA→AP, MIC=1, ACK=0, secure=0
  //   M3: AP→STA, ACK=1, MIC=1, secure=1
  //   M4: STA→AP, MIC=1, ACK=0, secure=1
  uint8_t msg = 0;
  if (fromDs && ackBit && !micPresent) msg = 1;
  else if (!fromDs && micPresent && !secureBit) msg = 2;
  else if (fromDs && ackBit && micPresent && secureBit) msg = 3;
  else if (!fromDs && micPresent && secureBit) msg = 4;

  if (msg == 0) return;

  // Store per-message state.
  if (msg == 1) {
    memcpy(s->anonce, body + 13, 32);
    s->haveAnonce = true;
    memcpy(s->replay, body + 5, 8);
    // PMKID from key data
    uint8_t pmkid[16];
    if (findPmkid(eapol, eapolLen, pmkid)) {
      memcpy(s->pmkid, pmkid, 16);
      s->havePmkid = true;
    }
  } else if (msg == 2 || msg == 4) {
    memcpy(s->mic, body + 77, 16);
    s->haveMic = true;
    memcpy(s->replay, body + 5, 8);
    // Store the EAPOL frame (used for hashcat's `eapol` field).
    const uint16_t storeLen = (eapolLen > sizeof(s->eapol)) ? sizeof(s->eapol) : eapolLen;
    memcpy(s->eapol, eapol, storeLen);
    s->eapolLen = storeLen;
    if (msg == 2 && !s->haveAnonce) {
      // SNonce is also in every message; for M2+M3 pair we don't have the
      // AP nonce yet, but ANonce from M3 will overwrite the field below.
      memcpy(s->anonce, body + 13, 32);   // placeholder — see msg 3 handling
    }
  } else if (msg == 3) {
    if (!s->haveAnonce) {
      memcpy(s->anonce, body + 13, 32);
      s->haveAnonce = true;
    }
    if (!s->haveMic) {
      memcpy(s->mic, body + 77, 16);
      s->haveMic = true;
      // For M2+M3, hashcat expects the EAPOL payload of the M2 frame (the
      // one that carries the MIC over the SNonce). If we have M3 but not M2,
      // we skip emitting — M2+M3 requires the M2 EAPOL.
    }
  }

  s->seen |= (1u << (msg - 1));

  // Emit conditions:
  //   PMKID         : M1 with PMKID present → TYPE=01
  //   M1+M2 pair    : both seen, MIC from M2
  //   M2+M3 pair    : both seen, MIC from M2, ANonce from M3
  //   M3+M4 pair    : both seen, MIC from M4
  String line;
  bool emit = false;
  uint8_t msgPair = 0;

  if (s->havePmkid) {
    line = buildPmkidLine(*s);
    emit = true;
    s_pmkidCount++;
  } else if ((s->seen & 0x03) == 0x03) {           // M1 + M2
    line = buildHandshakeLine(*s, 0x00);
    emit = true;
    msgPair = 0x00;
    s_handshakeCount++;
  } else if ((s->seen & 0x06) == 0x06 && s->eapolLen > 0) {  // M2 + M3
    line = buildHandshakeLine(*s, 0x02);
    emit = true;
    msgPair = 0x02;
    s_handshakeCount++;
  } else if ((s->seen & 0x0C) == 0x0C) {           // M3 + M4
    line = buildHandshakeLine(*s, 0x04);
    emit = true;
    msgPair = 0x04;
    s_handshakeCount++;
  }

  if (emit) {
    enqueueLine(line);
    // Suppress re-emitting this session for 60 s.
    s->dedupUntilMs = millis() + 60000UL;
    Serial.printf("[Hashcat] captured %s (%s <-> %s, msgpair %02x)\n",
                  s->havePmkid ? "PMKID" : "handshake",
                  apMac ? "AP" : "?", "STA", msgPair);
  }
}

void hashcatWriterTick() {
  if (!s_open) return;
  drainOutput();
}