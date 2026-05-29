#include "PowerManager.h"

#include <Arduino.h>

#include "AppConfig.h"
#include "AppState.h"

namespace {
volatile uint32_t lastValidSignalMs = 0;
volatile bool fullPowerRequested = false;
uint32_t appliedCpuMhz = 0;
bool fullPowerActive = false;

void applyCpuFrequency(uint32_t mhz) {
  const uint32_t actualMhz = getCpuFrequencyMhz();
  if (appliedCpuMhz == mhz && actualMhz == mhz) {
    return;
  }

  if (setCpuFrequencyMhz(mhz)) {
    appliedCpuMhz = mhz;
    Serial.print(F("CPU频率已切换为 "));
    Serial.print(mhz);
    Serial.println(F("MHz"));
    return;
  }

  Serial.print(F("CPU频率切换失败，目标 "));
  Serial.print(mhz);
  Serial.println(F("MHz"));
}
} // namespace

void powerManagerBegin() {
  appliedCpuMhz = getCpuFrequencyMhz();
  fullPowerActive = appliedCpuMhz >= CPU_FULL_POWER_MHZ;
}

void powerManagerEnterFindMode() {
  // 找车待机常态使用低频；收到有效蓝牙命令后再由主循环提升频率。
  lastValidSignalMs = 0;
  fullPowerRequested = false;
  fullPowerActive = false;
  applyCpuFrequency(CPU_POWER_SAVE_MHZ);
}

void powerManagerEnterOtaMode() {
  // OTA 模式涉及 WiFi、WebServer 和 Flash 写入，始终保持满频。
  lastValidSignalMs = millis();
  fullPowerRequested = false;
  fullPowerActive = true;
  applyCpuFrequency(CPU_FULL_POWER_MHZ);
}

void powerManagerNoteValidSignal() {
  if (currentMode != FIND_MODE) {
    return;
  }

  lastValidSignalMs = millis();
  fullPowerRequested = true;
}

void powerManagerUpdate() {
  const DeviceMode mode = currentMode;
  if (mode == OTA_MODE || mode == OTA_UPDATING) {
    if (!fullPowerActive || getCpuFrequencyMhz() != CPU_FULL_POWER_MHZ) {
      fullPowerActive = true;
      applyCpuFrequency(CPU_FULL_POWER_MHZ);
    }
    return;
  }

  if (mode != FIND_MODE) {
    return;
  }

  if (fullPowerRequested) {
    fullPowerRequested = false;
    fullPowerActive = true;
    applyCpuFrequency(CPU_FULL_POWER_MHZ);
    return;
  }

  const uint32_t lastSeenMs = lastValidSignalMs;
  if (fullPowerActive && lastSeenMs != 0 &&
      static_cast<uint32_t>(millis() - lastSeenMs) > CPU_FULL_POWER_HOLD_MS) {
    fullPowerActive = false;
    applyCpuFrequency(CPU_POWER_SAVE_MHZ);
  }
}
