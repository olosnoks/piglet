#pragma once

// LovyanGFX sprite that emulates TFT_eSPI's setViewport()/resetViewport():
// a drawing origin shift plus a clip rectangle. The Piglet UI draws page
// content in viewport-relative coordinates (origin at the top of the content
// area, below the header), which TFT_eSPI provides natively but LovyanGFX does
// not. This subclass adds the offset to every primitive the UI uses so the
// 100+ KB of shared UI code runs unchanged on the RGB panel.

#include "LGFX_W550.h"

class ViewportSprite : public lgfx::LGFX_Sprite {
  int32_t _ox = 0, _oy = 0;

public:
  ViewportSprite() : lgfx::LGFX_Sprite() {}
  explicit ViewportSprite(lgfx::LovyanGFX* parent) : lgfx::LGFX_Sprite(parent) {}

  void setViewport(int32_t x, int32_t y, int32_t w, int32_t h) {
    _ox = x; _oy = y;
    setClipRect(x, y, w, h);
  }
  void resetViewport() {
    _ox = 0; _oy = 0;
    clearClipRect();
  }

  using lgfx::LGFX_Sprite::drawPixel;
  template <typename T> void drawPixel(int32_t x, int32_t y, const T& c) {
    lgfx::LGFX_Sprite::drawPixel(x + _ox, y + _oy, c);
  }

  using lgfx::LGFX_Sprite::drawFastVLine;
  template <typename T> void drawFastVLine(int32_t x, int32_t y, int32_t h, const T& c) {
    lgfx::LGFX_Sprite::drawFastVLine(x + _ox, y + _oy, h, c);
  }

  using lgfx::LGFX_Sprite::drawFastHLine;
  template <typename T> void drawFastHLine(int32_t x, int32_t y, int32_t w, const T& c) {
    lgfx::LGFX_Sprite::drawFastHLine(x + _ox, y + _oy, w, c);
  }

  using lgfx::LGFX_Sprite::fillRect;
  template <typename T> void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, const T& c) {
    lgfx::LGFX_Sprite::fillRect(x + _ox, y + _oy, w, h, c);
  }

  using lgfx::LGFX_Sprite::drawRect;
  template <typename T> void drawRect(int32_t x, int32_t y, int32_t w, int32_t h, const T& c) {
    lgfx::LGFX_Sprite::drawRect(x + _ox, y + _oy, w, h, c);
  }

  using lgfx::LGFX_Sprite::drawRoundRect;
  template <typename T> void drawRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, const T& c) {
    lgfx::LGFX_Sprite::drawRoundRect(x + _ox, y + _oy, w, h, r, c);
  }

  using lgfx::LGFX_Sprite::fillRoundRect;
  template <typename T> void fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, const T& c) {
    lgfx::LGFX_Sprite::fillRoundRect(x + _ox, y + _oy, w, h, r, c);
  }

  using lgfx::LGFX_Sprite::drawCircle;
  template <typename T> void drawCircle(int32_t x, int32_t y, int32_t r, const T& c) {
    lgfx::LGFX_Sprite::drawCircle(x + _ox, y + _oy, r, c);
  }

  using lgfx::LGFX_Sprite::fillCircle;
  template <typename T> void fillCircle(int32_t x, int32_t y, int32_t r, const T& c) {
    lgfx::LGFX_Sprite::fillCircle(x + _ox, y + _oy, r, c);
  }

  using lgfx::LGFX_Sprite::drawLine;
  template <typename T> void drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, const T& c) {
    lgfx::LGFX_Sprite::drawLine(x0 + _ox, y0 + _oy, x1 + _ox, y1 + _oy, c);
  }

  using lgfx::LGFX_Sprite::fillTriangle;
  template <typename T> void fillTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                                          int32_t x2, int32_t y2, const T& c) {
    lgfx::LGFX_Sprite::fillTriangle(x0 + _ox, y0 + _oy, x1 + _ox, y1 + _oy,
                                    x2 + _ox, y2 + _oy, c);
  }

  using lgfx::LGFX_Sprite::drawTriangle;
  template <typename T> void drawTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                                          int32_t x2, int32_t y2, const T& c) {
    lgfx::LGFX_Sprite::drawTriangle(x0 + _ox, y0 + _oy, x1 + _ox, y1 + _oy,
                                    x2 + _ox, y2 + _oy, c);
  }

  using lgfx::LGFX_Sprite::drawBitmap;
  template <typename T> void drawBitmap(int32_t x, int32_t y, const uint8_t* bmp,
                                        int32_t w, int32_t h, const T& fg) {
    lgfx::LGFX_Sprite::drawBitmap(x + _ox, y + _oy, bmp, w, h, fg);
  }
  template <typename T> void drawBitmap(int32_t x, int32_t y, const uint8_t* bmp,
                                        int32_t w, int32_t h, const T& fg, const T& bg) {
    lgfx::LGFX_Sprite::drawBitmap(x + _ox, y + _oy, bmp, w, h, fg, bg);
  }

  // Text cursor (positions the subsequent print()/printf() output).
  using lgfx::LGFX_Sprite::setCursor;
  void setCursor(int32_t x, int32_t y) {
    lgfx::LGFX_Sprite::setCursor(x + _ox, y + _oy);
  }
  void setCursor(int32_t x, int32_t y, uint8_t font) {
    lgfx::LGFX_Sprite::setCursor(x + _ox, y + _oy, font);
  }
  void setCursor(int32_t x, int32_t y, const lgfx::IFont* font) {
    lgfx::LGFX_Sprite::setCursor(x + _ox, y + _oy, font);
  }

  using lgfx::LGFX_Sprite::drawString;
  size_t drawString(const char* s, int32_t x, int32_t y) {
    return lgfx::LGFX_Sprite::drawString(s, x + _ox, y + _oy);
  }
  size_t drawString(const char* s, int32_t x, int32_t y, uint8_t font) {
    return lgfx::LGFX_Sprite::drawString(s, x + _ox, y + _oy, font);
  }
  size_t drawString(const char* s, int32_t x, int32_t y, const lgfx::IFont* font) {
    return lgfx::LGFX_Sprite::drawString(s, x + _ox, y + _oy, font);
  }
  size_t drawString(const String& s, int32_t x, int32_t y) {
    return lgfx::LGFX_Sprite::drawString(s, x + _ox, y + _oy);
  }
  size_t drawString(const String& s, int32_t x, int32_t y, uint8_t font) {
    return lgfx::LGFX_Sprite::drawString(s, x + _ox, y + _oy, font);
  }
  size_t drawString(const String& s, int32_t x, int32_t y, const lgfx::IFont* font) {
    return lgfx::LGFX_Sprite::drawString(s, x + _ox, y + _oy, font);
  }
};
