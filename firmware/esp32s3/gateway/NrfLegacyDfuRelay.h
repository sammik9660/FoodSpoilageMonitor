#pragma once

#include <Arduino.h>
#include <BLEAdvertisedDevice.h>
#include <BLEClient.h>
#include <FS.h>

#include "NrfLegacyDfuProtocol.h"

class NrfLegacyDfuRelay {
 public:
  using LogFunction = void (*)(const char*, ...);
  using ProgressFunction = void (*)(uint32_t sent, uint32_t total, void* context);

  struct PackageInfo {
    uint32_t binaryOffset = 0;
    uint32_t binarySize = 0;
    uint32_t binaryZipCrc = 0;
    uint32_t datOffset = 0;
    uint32_t datSize = 0;
    uint32_t datZipCrc = 0;
    uint16_t firmwareCrc16 = 0;
    char binaryName[96] = {};
    char datName[96] = {};
  };

  void setLogger(LogFunction logger) { logger_ = logger; }
  bool validatePackage(fs::FS& fs, const char* path, PackageInfo& info, char* error, size_t errorSize);
  bool run(fs::FS& fs, const char* path, const PackageInfo& info, BLEClient* client,
           const BLEAddress& applicationAddress, volatile bool* cancelRequested,
           char* error, size_t errorSize, ProgressFunction progress = nullptr,
           void* progressContext = nullptr);
  static bool nearbyAddress(const String& application, const String& candidate);

 private:
  struct ZipEntry {
    char name[96] = {};
    uint32_t dataOffset = 0;
    uint32_t size = 0;
    uint32_t crc32 = 0;
  };

  LogFunction logger_ = nullptr;
  QueueHandle_t notifications_ = nullptr;
  BLERemoteCharacteristic* control_ = nullptr;
  BLERemoteCharacteristic* packet_ = nullptr;
  BLEAdvertisedDevice* bootloaderDevice_ = nullptr;
  String applicationAddress_;

  static NrfLegacyDfuRelay* active_;
  static void notifyCallback(BLERemoteCharacteristic*, uint8_t* data, size_t length, bool);
  bool waitResponse(uint8_t opcode, uint32_t timeoutMs, char* error, size_t errorSize);
  bool waitPrn(uint32_t bytes, uint32_t timeoutMs, char* error, size_t errorSize);
  bool writeControl(const uint8_t* data, size_t length, char* error, size_t errorSize);
  bool findBootloader(BLEClient* client, const BLEAddress& applicationAddress, char* error, size_t errorSize);
  bool streamBinary(File& package, const PackageInfo& info, volatile bool* cancelRequested,
                    char* error, size_t errorSize, ProgressFunction progress,
                    void* progressContext);
  static bool readEntry(File& package, const ZipEntry& entry, uint8_t* destination, size_t capacity);
  static bool verifyEntryCrc(File& package, const ZipEntry& entry);
  static bool locateEntries(File& package, ZipEntry& manifest, ZipEntry entries[], size_t& entryCount,
                            char* error, size_t errorSize);
  static void fail(char* error, size_t errorSize, const char* message);
};
