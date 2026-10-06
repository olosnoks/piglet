#pragma once

// Graphics backend selector.
//
// The Freenove S3 CYD drives an ILI9341 over SPI via TFT_eSPI. The Guition
// JC8048W550 drives an 800x480 RGB parallel panel, which TFT_eSPI cannot do, so
// that board uses LovyanGFX instead. LovyanGFX is a deliberate TFT_eSPI-compatible
// superset (same drawString / fillRect / pushSprite / setTextDatum / datum
// constants), so the UI code draws through these aliases unchanged.

#if defined(BOARD_W550)
  #include "LGFX_W550.h"
  #include "ViewportSprite.h"
  using GfxDevice = LGFX;
  using GfxSprite = ViewportSprite;  // LGFX_Sprite + TFT_eSPI-style viewport
#else
  #include <TFT_eSPI.h>
  using GfxDevice = TFT_eSPI;
  using GfxSprite = TFT_eSprite;
#endif
