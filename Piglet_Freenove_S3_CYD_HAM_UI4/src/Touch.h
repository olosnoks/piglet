#pragma once

#include <Arduino.h>
#include <Wire.h>

struct TouchPoint {
  bool pressed = false;
  int16_t x = 0;
  int16_t y = 0;
};

class FT6336Touch {
public:
  void begin();
  TouchPoint read();

private:
  static constexpr uint8_t ADDRESS = 0x38;

  void readBytes(uint8_t reg, uint8_t* data, size_t len);
};
