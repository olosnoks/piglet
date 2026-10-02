#pragma once
#include <Arduino.h>

// Apple Continuity / Proximity-Pairing advertisement spam. Broadcasts spoofed
// Apple BLE adverts that make nearby iOS devices show pairing popups. Uses the
// existing NimBLE stack (shared with the wardriving BLE scanner).

void     appleBleSpamBegin();
void     appleBleSpamStart();
void     appleBleSpamStop();
bool     appleBleSpamActive();
uint32_t appleBleSpamCount();
void     appleBleSpamTick();
