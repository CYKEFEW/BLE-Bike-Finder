#include "ModeIndicator.h"

#include <Arduino.h>

#include "AppConfig.h"

void modeIndicatorBegin() {
  pinMode(OTA_MODE_LED_PIN, OUTPUT);
  setOtaModeIndicator(false);
}

void setOtaModeIndicator(bool enabled) {
  digitalWrite(OTA_MODE_LED_PIN, enabled ? HIGH : LOW);
}
