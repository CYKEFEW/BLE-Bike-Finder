#include <Arduino.h>
#include <WiFi.h>

#include "AppState.h"
#include "BleScanner.h"
#include "BuzzerController.h"
#include "ModeIndicator.h"
#include "OtaWebServer.h"

volatile DeviceMode currentMode = FIND_MODE;
volatile bool otaActivateRequested = false;
volatile bool otaCancelRequested = false;
volatile int lastFinderRssi = -127;
volatile uint32_t lastFinderSeenMs = 0;

namespace {
bool restartScheduled = false;
uint32_t restartAtMs = 0;

void enterFindMode() {
  currentMode = FIND_MODE;
  otaActivateRequested = false;
  otaCancelRequested = false;
  lastFinderSeenMs = 0;
  buzzerResetSignalState();
  setOtaModeIndicator(false);

  otaWebStop();
  startBleScan();

  Serial.println(F("已进入找车模式"));
}

void enterOtaMode() {
  currentMode = OTA_MODE;
  otaActivateRequested = false;
  otaCancelRequested = false;
  buzzerOff();
  setOtaModeIndicator(true);

  otaWebBegin();
  startBleScan();
}

void handleScheduledRestart() {
  if (!restartScheduled || static_cast<int32_t>(millis() - restartAtMs) < 0) {
    return;
  }

  buzzerOff();
  setOtaModeIndicator(false);
  stopBleScan();
  otaWebStop();
  delay(50);
  ESP.restart();
}
} // namespace

void scheduleRestart(uint32_t delayMs) {
  restartScheduled = true;
  restartAtMs = millis() + delayMs;
}

void setup() {
  buzzerBegin();
  modeIndicatorBegin();
  Serial.begin(115200);
  delay(100);
  enterFindMode();
}

void loop() {
  handleScheduledRestart();

  if (otaActivateRequested && currentMode != OTA_UPDATING) {
    enterOtaMode();
  }

  if (otaCancelRequested && currentMode == OTA_MODE) {
    enterFindMode();
  }

  if (currentMode == FIND_MODE) {
    startBleScan();
    buzzerUpdate(lastFinderRssi, lastFinderSeenMs);
    delay(5);
    return;
  }

  if (currentMode == OTA_MODE) {
    startBleScan();
    otaWebHandleClient();
    buzzerOff();
    setOtaModeIndicator(true);
    delay(2);
    return;
  }

  otaWebHandleClient();
  buzzerOff();
  setOtaModeIndicator(true);
  delay(1);
}
