#pragma once

#include <Arduino.h>

void buzzerBegin();
void buzzerOff();
void buzzerResetSignalState();
void buzzerUpdate(int rssi, uint32_t lastSeenMs);
