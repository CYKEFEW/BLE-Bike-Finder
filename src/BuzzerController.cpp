#include "BuzzerController.h"

#include "AppConfig.h"

namespace {
bool buzzerOn = false;
bool hasTrackedSignal = false;
bool lostAlertActive = false;
bool lostAlertDone = false;
uint8_t lostAlertPhase = 0;
uint32_t trackedSignalSeenMs = 0;
uint32_t nextBuzzerChangeMs = 0;

void setBuzzer(bool enabled) {
  buzzerOn = enabled;
  digitalWrite(BUZZER_PIN, enabled ? HIGH : LOW);
}

bool timeReached(uint32_t now, uint32_t targetMs) {
  return static_cast<int32_t>(now - targetMs) >= 0;
}

bool isNewerTimestamp(uint32_t value, uint32_t reference) {
  return reference == 0 || static_cast<int32_t>(value - reference) > 0;
}

// RSSI 在近距离阈值和极限阈值之间线性映射，避免距离变化时蜂鸣节奏跳变。
uint32_t smoothPulseCycleForRssi(int rssi) {
  if (rssi >= RSSI_NEAR_THRESHOLD) {
    return BUZZER_SLOWEST_CYCLE_MS;
  }
  if (rssi <= RSSI_LIMIT_THRESHOLD) {
    return BUZZER_FASTEST_CYCLE_MS;
  }

  const int range = RSSI_NEAR_THRESHOLD - RSSI_LIMIT_THRESHOLD;
  const int distanceFromNear = RSSI_NEAR_THRESHOLD - rssi;
  const uint32_t cycleRange = BUZZER_SLOWEST_CYCLE_MS - BUZZER_FASTEST_CYCLE_MS;
  return BUZZER_SLOWEST_CYCLE_MS - (cycleRange * distanceFromNear / range);
}

void startLostAlert(uint32_t now) {
  lostAlertActive = true;
  lostAlertDone = false;
  lostAlertPhase = 0;
  nextBuzzerChangeMs = now + BUZZER_LOST_ALERT_STEP_MS;
  setBuzzer(true);
}

void updateLostAlert(uint32_t now) {
  if (!lostAlertActive) {
    setBuzzer(false);
    return;
  }

  if (!timeReached(now, nextBuzzerChangeMs)) {
    return;
  }

  lostAlertPhase++;
  if (lostAlertPhase >= BUZZER_LOST_ALERT_PHASES) {
    lostAlertActive = false;
    lostAlertDone = true;
    setBuzzer(false);
    return;
  }

  setBuzzer((lostAlertPhase % 2) == 0);
  nextBuzzerChangeMs = now + BUZZER_LOST_ALERT_STEP_MS;
}

void armNewSignal(uint32_t seenMs) {
  hasTrackedSignal = true;
  lostAlertActive = false;
  lostAlertDone = false;
  trackedSignalSeenMs = seenMs;
  nextBuzzerChangeMs = 0;
}
} // namespace

void buzzerBegin() {
  pinMode(BUZZER_PIN, OUTPUT);
  buzzerResetSignalState();
}

void buzzerOff() {
  lostAlertActive = false;
  setBuzzer(false);
}

void buzzerResetSignalState() {
  hasTrackedSignal = false;
  lostAlertActive = false;
  lostAlertDone = false;
  lostAlertPhase = 0;
  trackedSignalSeenMs = 0;
  nextBuzzerChangeMs = 0;
  setBuzzer(false);
}

void buzzerUpdate(int rssi, uint32_t lastSeenMs) {
  const uint32_t now = millis();

  if (lastSeenMs == 0) {
    buzzerResetSignalState();
    return;
  }

  if (isNewerTimestamp(lastSeenMs, trackedSignalSeenMs)) {
    armNewSignal(lastSeenMs);
  }

  if (!hasTrackedSignal) {
    setBuzzer(false);
    return;
  }

  if (now - trackedSignalSeenMs > FIND_TIMEOUT_MS) {
    if (!lostAlertActive && !lostAlertDone) {
      startLostAlert(now);
    }
    updateLostAlert(now);
    return;
  }

  if (rssi < RSSI_LIMIT_THRESHOLD) {
    lostAlertActive = false;
    lostAlertDone = false;
    setBuzzer(true);
    return;
  }

  const uint32_t cycleMs = smoothPulseCycleForRssi(rssi);
  const uint32_t offMs = cycleMs > BUZZER_PULSE_ON_MS ? cycleMs - BUZZER_PULSE_ON_MS : BUZZER_PULSE_ON_MS;

  if (nextBuzzerChangeMs == 0 || timeReached(now, nextBuzzerChangeMs)) {
    const bool nextState = !buzzerOn;
    setBuzzer(nextState);
    nextBuzzerChangeMs = now + (nextState ? BUZZER_PULSE_ON_MS : offMs);
  }
}
