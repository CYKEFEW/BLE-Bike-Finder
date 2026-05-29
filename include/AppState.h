#pragma once

#include <Arduino.h>

enum DeviceMode {
  FIND_MODE,
  OTA_MODE,
  OTA_UPDATING
};

extern volatile DeviceMode currentMode;
extern volatile bool otaActivateRequested;
extern volatile bool otaCancelRequested;
extern volatile int lastFinderRssi;
extern volatile uint32_t lastFinderSeenMs;

void scheduleRestart(uint32_t delayMs = 1000);
