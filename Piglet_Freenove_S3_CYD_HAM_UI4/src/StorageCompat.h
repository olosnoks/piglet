#pragma once

#include <FS.h>
#include <SD_MMC.h>

// Ham's Piglet modules use the Arduino SD FS interface. SD_MMC implements
// the same FS operations used by Piglet, so keep the backend source intact.
#define SD SD_MMC
