#pragma once

#include <Arduino.h>

namespace BoardPins {
static constexpr int BACKLIGHT_PIN = 45;

static constexpr int TOUCH_SDA = 16;
static constexpr int TOUCH_SCL = 15;
static constexpr int TOUCH_RST = 18;
static constexpr int TOUCH_INT = 17;

static constexpr int GPS_RX = 44;
static constexpr int GPS_TX = 43;

static constexpr int SD_CMD = 40;
static constexpr int SD_CLK = 38;
static constexpr int SD_D0  = 39;
static constexpr int SD_D1  = 41;
static constexpr int SD_D2  = 48;
static constexpr int SD_D3  = 47;
}
