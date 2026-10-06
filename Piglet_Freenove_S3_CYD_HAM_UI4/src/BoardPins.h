#pragma once

#include <Arduino.h>

namespace BoardPins {
#if defined(BOARD_W550)
// Guition JC8048W550. The RGB panel + GT911 touch pins are owned by LovyanGFX
// (see LGFX_W550.h); only the non-display peripherals are declared here.
static constexpr int BACKLIGHT_PIN = 2;   // handled by LovyanGFX Light_PWM too

// Touch is driven by LovyanGFX (GT911 on I2C1 SDA19/SCL20); these legacy FT6336
// fields are unused on this board but kept so Touch.cpp still compiles.
static constexpr int TOUCH_SDA = 19;
static constexpr int TOUCH_SCL = 20;
static constexpr int TOUCH_RST = -1;
static constexpr int TOUCH_INT = 18;

// GPS on the broken-out UART1 header (free pins, clear of the RGB bus).
static constexpr int GPS_RX = 44;
static constexpr int GPS_TX = 43;

// Onboard TF slot is wired for SPI (not SDIO) on this board.
static constexpr int SD_CS   = 10;
static constexpr int SD_MISO = 11;
static constexpr int SD_SCK  = 12;
static constexpr int SD_MOSI = 13;
#else
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
#endif
}
