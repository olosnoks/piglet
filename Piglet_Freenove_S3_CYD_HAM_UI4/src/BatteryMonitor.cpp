#include "BatteryMonitor.h"

#include "Globals.h"

namespace {
constexpr uint32_t SAMPLE_INTERVAL_MS = 1000;
constexpr int SAMPLES_PER_READ = 16;
constexpr float DIVIDER_RATIO = 2.0f;
constexpr float SMOOTHING = 0.2f;

struct CurvePoint {
  float volts;
  int percent;
};

// Typical single-cell LiPo discharge curve under light load.
const CurvePoint LIPO_CURVE[] = {
  {4.20f, 100}, {4.10f, 90}, {4.00f, 78}, {3.90f, 63}, {3.80f, 40},
  {3.75f, 25},  {3.70f, 13}, {3.60f, 5},  {3.30f, 0},
};

int activePin = -1;
float smoothedVolts = 0.0f;
bool haveReading = false;
uint32_t lastSampleMs = 0;

// ESP32-S3 ADC pins are GPIO 1-20.
bool validAdcPin(int pin) {
  return pin >= 1 && pin <= 20;
}
}

void batteryMonitorTick() {
  if (!validAdcPin(cfg.battPin)) {
    activePin = -1;
    haveReading = false;
    return;
  }

  if (cfg.battPin != activePin) {
    activePin = cfg.battPin;
    pinMode(activePin, INPUT);
    haveReading = false;
    lastSampleMs = 0;
  }

  if (haveReading && millis() - lastSampleMs < SAMPLE_INTERVAL_MS) return;
  lastSampleMs = millis();

  uint32_t totalMv = 0;
  for (int i = 0; i < SAMPLES_PER_READ; ++i) {
    totalMv += analogReadMilliVolts(activePin);
  }
  const float volts = totalMv / static_cast<float>(SAMPLES_PER_READ) * DIVIDER_RATIO / 1000.0f;

  smoothedVolts = haveReading ? smoothedVolts + (volts - smoothedVolts) * SMOOTHING : volts;
  haveReading = true;
}

bool batteryMonitorAvailable() {
  return haveReading;
}

float batteryVoltage() {
  return smoothedVolts;
}

int batteryPercent() {
  constexpr int last = sizeof(LIPO_CURVE) / sizeof(LIPO_CURVE[0]) - 1;
  if (smoothedVolts >= LIPO_CURVE[0].volts) return 100;
  if (smoothedVolts <= LIPO_CURVE[last].volts) return 0;

  for (int i = 1; i <= last; ++i) {
    const CurvePoint& hi = LIPO_CURVE[i - 1];
    const CurvePoint& lo = LIPO_CURVE[i];
    if (smoothedVolts >= lo.volts) {
      const float t = (smoothedVolts - lo.volts) / (hi.volts - lo.volts);
      return lo.percent + static_cast<int>(t * (hi.percent - lo.percent) + 0.5f);
    }
  }
  return 0;
}
