#pragma once
#include <Arduino.h>

struct PinMap {
  int sda, scl;
  int sd_cs, sd_sck, sd_miso, sd_mosi;
  int gps_rx, gps_tx;
  int btn;
  const char* name;
};

static const PinMap PINS_S3 = {
  16, 15,
  -1, -1, -1, -1,
  44, 43,
  -1,
  "FREENOVE_S3_CYD"
};

// The target is S3-only. These aliases remain because Ham's shared config
// parser references every supported Piglet board symbol.
static const PinMap PINS_C6 = {
  16, 15, -1, -1, -1, -1, 44, 43, -1, "FREENOVE_S3_CYD"
};
static const PinMap PINS_C5 = {
  16, 15, -1, -1, -1, -1, 44, 43, -1, "FREENOVE_S3_CYD"
};
static const PinMap PINS_S3_EXP_BASE = {
  16, 15, -1, -1, -1, -1, 44, 43, -1, "FREENOVE_S3_CYD"
};
static const PinMap PINS_XIAO_C3 = {
  16, 15, -1, -1, -1, -1, 44, 43, -1, "FREENOVE_S3_CYD"
};
