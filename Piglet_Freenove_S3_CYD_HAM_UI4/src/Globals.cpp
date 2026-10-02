#include "Globals.h"

PinMap pins = PINS_S3;
Config cfg;
TinyGPSPlus gps;
HardwareSerial GPSSerial(1);
WebServer server(80);

bool sdOk = false;
bool scanningEnabled = true;
bool gpsHasFix = false;
bool allowScanForOled = false;
bool userScanOverride = false;
bool autoPaused = false;
bool radioIdle = false;

uint8_t currentPage = 0;
bool statusPagePaused = false;

uint32_t apStartMs = 0;
bool apClientSeen = false;
bool apWindowActive = false;
const uint32_t AP_WINDOW_MS = 60000UL;

bool apExtended = false;
uint32_t apExtendedStartMs = 0;
bool apForceClose = false;
const uint32_t AP_EXTENDED_WINDOW_MS = 5UL * 60UL * 1000UL;
const uint32_t AP_EXTEND_PROMPT_LEAD_MS = 30000UL;

uint32_t networksFound2G = 0;
uint32_t networksFound5G = 0;
uint32_t devicesFoundBle = 0;

File logFile;
String currentCsvPath;
wl_status_t lastStaStatus = WL_IDLE_STATUS;

bool uploading = false;
bool uploadPausedScanWasEnabled = false;
uint32_t uploadTotalFiles = 0;
uint32_t uploadDoneFiles = 0;
String uploadCurrentFile;
String uploadLastResult;
String uploadTargetName;
uint32_t uploadFailedFiles = 0;
int wigleTokenStatus = 0;
int wigleLastHttpCode = 0;

const char* WIGLE_HOST = "api.wigle.net";
const uint16_t WIGLE_PORT = 443;
