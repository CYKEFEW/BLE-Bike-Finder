#include "BleScanner.h"

#include <Arduino.h>
#include <NimBLEDevice.h>

#include "AppConfig.h"
#include "AppState.h"

namespace {
NimBLEScan *bleScan = nullptr;
uint8_t pendingModeCommand = 0;
uint8_t pendingModeCommandCount = 0;
uint32_t pendingModeCommandStartedMs = 0;
uint32_t ignoreFindUntilMs = 0;
bool hasStoppedFindSession = false;
uint8_t stoppedFindSession = 0;

uint8_t checksumPayloadWithoutCompanyId(const uint8_t *payload, size_t length) {
  uint8_t checksum = 0;
  for (size_t i = 0; i < length; i++) {
    checksum ^= payload[i];
  }
  return checksum;
}

bool isInGuardWindow(uint32_t now, uint32_t untilMs) {
  return static_cast<int32_t>(now - untilMs) < 0;
}

bool confirmModeCommand(uint8_t command, uint32_t now) {
  if (pendingModeCommand != command ||
      static_cast<uint32_t>(now - pendingModeCommandStartedMs) > BLE_MODE_COMMAND_CONFIRM_WINDOW_MS) {
    pendingModeCommand = command;
    pendingModeCommandCount = 1;
    pendingModeCommandStartedMs = now;
    ignoreFindUntilMs = now + BLE_COMMAND_GUARD_MS;
    return BLE_MODE_COMMAND_CONFIRM_COUNT <= 1;
  }

  pendingModeCommandCount++;
  ignoreFindUntilMs = now + BLE_COMMAND_GUARD_MS;

  if (pendingModeCommandCount < BLE_MODE_COMMAND_CONFIRM_COUNT) {
    return false;
  }

  pendingModeCommand = 0;
  pendingModeCommandCount = 0;
  pendingModeCommandStartedMs = 0;
  return true;
}

bool parseBikeFinderCommand(const NimBLEAdvertisedDevice *device, uint8_t &command, uint8_t &session) {
  if (!device->haveManufacturerData()) {
    return false;
  }

  const std::string manufacturerData = device->getManufacturerData();
  if (manufacturerData.size() < BLE_PAYLOAD_WITH_CHECKSUM_SIZE) {
    return false;
  }

  const uint8_t *data = reinterpret_cast<const uint8_t *>(manufacturerData.data());
  const uint16_t companyId = static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8);
  if (companyId != BLE_COMPANY_ID) {
    return false;
  }

  size_t offset = 2;
  if (memcmp(data + offset, BLE_MAGIC, sizeof(BLE_MAGIC)) != 0) {
    return false;
  }

  offset += sizeof(BLE_MAGIC);
  if (data[offset++] != BLE_PROTOCOL_VERSION) {
    return false;
  }

  if (memcmp(data + offset, BLE_DEVICE_ID, sizeof(BLE_DEVICE_ID)) != 0) {
    return false;
  }

  offset += sizeof(BLE_DEVICE_ID);
  command = data[offset];
  session = data[offset + 1];
  const uint8_t expectedChecksum = data[offset + 2];
  const uint8_t actualChecksum = checksumPayloadWithoutCompanyId(data + 2, BLE_PAYLOAD_SIZE - 2);
  if (actualChecksum != expectedChecksum) {
    return false;
  }

  return true;
}

void logValidCommand(uint8_t command, uint8_t session, int rssi) {
  Serial.print(F("BLE有效命令 cmd=0x"));
  Serial.print(command, HEX);
  Serial.print(F(" session="));
  Serial.print(session);
  Serial.print(F(" rssi="));
  Serial.println(rssi);
}

class BikeFinderScanCallbacks : public NimBLEScanCallbacks {
public:
  void onResult(const NimBLEAdvertisedDevice *advertisedDevice) override {
    uint8_t command = 0;
    uint8_t session = 0;
    if (!parseBikeFinderCommand(advertisedDevice, command, session)) {
      return;
    }

    const uint32_t now = millis();
    const DeviceMode mode = currentMode;
    logValidCommand(command, session, advertisedDevice->getRSSI());
    if (command == BLE_COMMAND_FIND && mode == FIND_MODE) {
      if (hasStoppedFindSession && session == stoppedFindSession) {
        return;
      }
      if (isInGuardWindow(now, ignoreFindUntilMs)) {
        return;
      }
      lastFinderRssi = advertisedDevice->getRSSI();
      lastFinderSeenMs = now;
      return;
    }

    if (command == BLE_COMMAND_FIND_OFF && mode == FIND_MODE) {
      hasStoppedFindSession = true;
      stoppedFindSession = session;
      lastFinderSeenMs = 0;
      ignoreFindUntilMs = now + BLE_COMMAND_GUARD_MS;
      return;
    }

    if (command == BLE_COMMAND_OTA_ON && mode == FIND_MODE && confirmModeCommand(command, now)) {
      hasStoppedFindSession = false;
      lastFinderSeenMs = 0;
      otaActivateRequested = true;
      return;
    }

    if (command == BLE_COMMAND_OTA_OFF && mode == OTA_MODE && confirmModeCommand(command, now)) {
      otaCancelRequested = true;
    }
  }
};

BikeFinderScanCallbacks scanCallbacks;
} // namespace

void stopBleScan() {
  if (bleScan != nullptr && bleScan->isScanning()) {
    bleScan->stop();
  }
}

void startBleScan() {
  if (bleScan == nullptr) {
    NimBLEDevice::init("");
    bleScan = NimBLEDevice::getScan();
    bleScan->setScanCallbacks(&scanCallbacks, true);
    bleScan->setActiveScan(false);
    bleScan->setInterval(160);
    bleScan->setWindow(80);
    bleScan->setMaxResults(16);
  }

  if (!bleScan->isScanning()) {
    bleScan->clearResults();
    bleScan->start(0, false, true);
    Serial.println(F("BLE扫描已启动"));
  }
}
