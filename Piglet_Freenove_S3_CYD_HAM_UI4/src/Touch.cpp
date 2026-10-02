#include "Touch.h"
#include "BoardPins.h"

void FT6336Touch::begin() {
  pinMode(BoardPins::TOUCH_RST, OUTPUT);
  digitalWrite(BoardPins::TOUCH_RST, LOW);
  delay(10);
  digitalWrite(BoardPins::TOUCH_RST, HIGH);
  delay(120);

  pinMode(BoardPins::TOUCH_INT, INPUT_PULLUP);
  Wire.begin(BoardPins::TOUCH_SDA, BoardPins::TOUCH_SCL);
  Wire.setClock(400000);
}

void FT6336Touch::readBytes(uint8_t reg, uint8_t* data, size_t len) {
  Wire.beginTransmission(ADDRESS);
  Wire.write(reg);

  if (Wire.endTransmission(false) != 0) {
    memset(data, 0, len);
    return;
  }

  Wire.requestFrom(static_cast<int>(ADDRESS), static_cast<int>(len));

  size_t i = 0;
  while (Wire.available() && i < len) {
    data[i++] = Wire.read();
  }

  while (i < len) data[i++] = 0;
}

TouchPoint FT6336Touch::read() {
  uint8_t count = 0;
  readBytes(0x02, &count, 1);
  count &= 0x0F;

  if (count == 0) return {};

  uint8_t data[4] = {};
  readBytes(0x03, data, sizeof(data));

  const uint16_t rawX = ((data[0] & 0x0F) << 8) | data[1];
  const uint16_t rawY = ((data[2] & 0x0F) << 8) | data[3];

  TouchPoint point;
  point.pressed = true;
  point.x = constrain(static_cast<int16_t>(rawX), 0, 239);
  point.y = constrain(static_cast<int16_t>(rawY), 0, 319);
  return point;
}
