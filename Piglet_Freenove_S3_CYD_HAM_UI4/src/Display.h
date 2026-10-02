#pragma once

#include <Arduino.h>

void displayBegin();
void displayLoop(float speedValue);
void displayHandleTouch();
void displayRequestRedraw();
void displayTriggerHomeUpload();
void displayToggleCore();
void displayEnterRadioIdle();

// Compatibility hooks used by Ham's existing modules.
void updateOLED(float speedValue);
void showSplashScreen();
void pigAnimTick();
void pigTwerkStart();
void sasquatchStart();
void oledProgressBar(int x, int y, int w, int h, float pct);
