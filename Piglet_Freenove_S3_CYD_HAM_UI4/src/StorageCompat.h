#pragma once

#include <FS.h>

// Ham's Piglet modules use the Arduino SD FS interface via a global named `SD`.
// The Freenove board uses the SD_MMC (4-bit SDIO) backend; the Guition
// JC8048W550 has no free SDIO pins, so its onboard TF slot is driven over SPI
// using the standard SD library (whose global is already named `SD`).
#if defined(BOARD_W550)
  #include <SD.h>
#else
  #include <SD_MMC.h>
  #define SD SD_MMC
#endif
