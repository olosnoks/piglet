#include "Display.h"
#include "ScreenUI.h"

void displayBegin() {
  screenUI.begin();
}

void displayLoop(float speedValue) {
  screenUI.update(speedValue);
}

void displayHandleTouch() {
  screenUI.handleTouch();
}

void displayRequestRedraw() {
  screenUI.requestRedraw();
}

void displayTriggerHomeUpload() {
  screenUI.triggerHomeUpload();
}

void displayToggleCore() {
  screenUI.triggerCoreMode();
}

void displayEnterRadioIdle() {
  screenUI.triggerRadioIdle();
}

void updateOLED(float speedValue) {
  screenUI.update(speedValue);
}

void showSplashScreen() {
  screenUI.showSplash();
}

void pigAnimTick() {
  screenUI.update(0.0f);
}

void pigTwerkStart() {
  screenUI.requestRedraw();
}

void sasquatchStart() {
  screenUI.requestRedraw();
}

void oledProgressBar(int, int, int, int, float) {
}
