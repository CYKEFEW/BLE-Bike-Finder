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
bool hasFilteredRssi = false;
int filteredRssi = 0;

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

int stableRssiForBuzzer(int rawRssi) {
  if (!hasFilteredRssi) {
    hasFilteredRssi = true;
    filteredRssi = rawRssi;
    return filteredRssi;
  }

  const int delta = rawRssi - filteredRssi;
  if (abs(delta) <= RSSI_FILTER_DEADBAND_DBM) {
    return filteredRssi;
  }

  int step = delta * RSSI_FILTER_ALPHA_NUM / RSSI_FILTER_ALPHA_DEN;
  if (step == 0) {
    step = delta > 0 ? 1 : -1;
  }
  filteredRssi += step;
  return filteredRssi;
}

// RSSI 在近距离端点和极限距离端点之间线性映射，避免距离变化时蜂鸣节奏跳变。
uint32_t smoothPulseCycleForRssi(int rssi) {
  if (rssi >= RSSI_SLOW_EDGE_DBM) {
    return BUZZER_SLOW_EDGE_CYCLE_MS;
  }
  if (rssi <= RSSI_CONTINUOUS_EDGE_DBM) {
    return BUZZER_FAST_EDGE_CYCLE_MS;
  }

  const int range = RSSI_SLOW_EDGE_DBM - RSSI_CONTINUOUS_EDGE_DBM;
  const int distanceFromNear = RSSI_SLOW_EDGE_DBM - rssi;
  const uint32_t cycleRange = BUZZER_SLOW_EDGE_CYCLE_MS - BUZZER_FAST_EDGE_CYCLE_MS;
  return BUZZER_SLOW_EDGE_CYCLE_MS - (cycleRange * distanceFromNear / range);
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

void updateTrackedSignal(uint32_t seenMs) {
  if (!hasTrackedSignal || lostAlertActive || lostAlertDone) {
    hasFilteredRssi = false;
    armNewSignal(seenMs);
    return;
  }

  // 持续收到广播时只更新时间戳，不重置蜂鸣节奏，否则节奏会被 BLE 广播间隔扰动。
  trackedSignalSeenMs = seenMs;
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
  hasFilteredRssi = false;
  filteredRssi = 0;
  setBuzzer(false);
}

void buzzerUpdate(int rssi, uint32_t lastSeenMs) {
  const uint32_t now = millis();

  if (lastSeenMs == 0) {
    buzzerResetSignalState();
    return;
  }

  if (isNewerTimestamp(lastSeenMs, trackedSignalSeenMs)) {
    updateTrackedSignal(lastSeenMs);
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

  const int stableRssi = stableRssiForBuzzer(rssi);
  if (stableRssi < RSSI_CONTINUOUS_EDGE_DBM) {
    lostAlertActive = false;
    lostAlertDone = false;
    setBuzzer(true);
    return;
  }

  const uint32_t cycleMs = smoothPulseCycleForRssi(stableRssi);
  const uint32_t offMs = cycleMs > BUZZER_PULSE_ON_MS ? cycleMs - BUZZER_PULSE_ON_MS : BUZZER_PULSE_ON_MS;

  if (nextBuzzerChangeMs == 0 || timeReached(now, nextBuzzerChangeMs)) {
    const bool nextState = !buzzerOn;
    setBuzzer(nextState);
    nextBuzzerChangeMs = now + (nextState ? BUZZER_PULSE_ON_MS : offMs);
  }
}
