#include "NrfLegacyDfuRelay.h"

#include <BLEDevice.h>
#include <BLERemoteCharacteristic.h>
#include <BLERemoteService.h>
#include <BLEScan.h>
#include <cJSON.h>

using namespace nrf_legacy_dfu;

namespace {
BLEUUID kDfuService("00001530-1212-EFDE-1523-785FEABCD123");
BLEUUID kDfuControl("00001531-1212-EFDE-1523-785FEABCD123");
BLEUUID kDfuPacket("00001532-1212-EFDE-1523-785FEABCD123");

class DfuScanCallbacks : public BLEAdvertisedDeviceCallbacks {
 public:
  DfuScanCallbacks(BLEAdvertisedDevice*& result, const String& applicationAddress)
      : result_(result), applicationAddress_(applicationAddress) {}

  void onResult(BLEAdvertisedDevice device) override {
    if (result_ || !device.haveServiceUUID() || !device.isAdvertisingService(kDfuService)) return;
    if (!NrfLegacyDfuRelay::nearbyAddress(applicationAddress_, device.getAddress().toString())) return;
    result_ = new BLEAdvertisedDevice(device);
    BLEDevice::getScan()->stop();
  }

 private:
  BLEAdvertisedDevice*& result_;
  String applicationAddress_;
};

bool jsonUInt(cJSON* object, const char* name, uint32_t& value) {
  cJSON* item = cJSON_GetObjectItemCaseSensitive(object, name);
  if (!cJSON_IsNumber(item) || item->valuedouble < 0 || item->valuedouble > 4294967295.0) return false;
  value = static_cast<uint32_t>(item->valuedouble);
  return true;
}
}  // namespace

NrfLegacyDfuRelay* NrfLegacyDfuRelay::active_ = nullptr;

void NrfLegacyDfuRelay::fail(char* error, size_t errorSize, const char* message) {
  if (error && errorSize) snprintf(error, errorSize, "%s", message);
}

bool NrfLegacyDfuRelay::nearbyAddress(const String& application, const String& candidate) {
  auto parse = [](const String& text, uint64_t& value) {
    if (text.length() != 17) return false;
    value = 0;
    for (size_t i = 0; i < text.length(); ++i) {
      if (text[i] == ':') continue;
      const char c = text[i];
      const int nibble = c >= '0' && c <= '9' ? c - '0' :
                         c >= 'a' && c <= 'f' ? c - 'a' + 10 :
                         c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
      if (nibble < 0) return false;
      value = (value << 4) | static_cast<uint64_t>(nibble);
    }
    return true;
  };
  uint64_t a = 0, b = 0;
  if (!parse(application, a) || !parse(candidate, b)) return false;
  return a == b || a + 1 == b || b + 1 == a;
}

void NrfLegacyDfuRelay::notifyCallback(BLERemoteCharacteristic*, uint8_t* data, size_t length, bool) {
  if (!active_ || !active_->notifications_ || !length) return;
  Notification notification = {};
  notification.length = static_cast<uint8_t>(length > sizeof(notification.data) ? sizeof(notification.data) : length);
  memcpy(notification.data, data, notification.length);
  xQueueSend(active_->notifications_, &notification, 0);
}

bool NrfLegacyDfuRelay::locateEntries(File& package, ZipEntry& manifest, ZipEntry entries[],
                                      size_t& entryCount, char* error, size_t errorSize) {
  entryCount = 0;
  package.seek(0);
  while (package.available()) {
    uint8_t header[30];
    if (package.read(header, sizeof(header)) != sizeof(header)) break;
    const uint32_t signature = readLe32(header);
    if (signature == 0x02014b50UL || signature == 0x06054b50UL) break;
    if (signature != 0x04034b50UL) { fail(error, errorSize, "ZIP local header invalid"); return false; }
    const uint16_t flags = readLe16(header + 6);
    const uint16_t method = readLe16(header + 8);
    const uint32_t crc = readLe32(header + 14);
    const uint32_t compressed = readLe32(header + 18);
    const uint32_t uncompressed = readLe32(header + 22);
    const uint16_t nameLength = readLe16(header + 26);
    const uint16_t extraLength = readLe16(header + 28);
    if ((flags & 0x0009) || method != 0 || compressed != uncompressed || !nameLength || nameLength >= 96) {
      fail(error, errorSize, "ZIP must use unencrypted STORE entries without data descriptors"); return false;
    }
    ZipEntry entry;
    if (package.read(reinterpret_cast<uint8_t*>(entry.name), nameLength) != nameLength) {
      fail(error, errorSize, "ZIP entry name truncated"); return false;
    }
    entry.name[nameLength] = 0;
    if (!package.seek(package.position() + extraLength)) { fail(error, errorSize, "ZIP extra field invalid"); return false; }
    entry.dataOffset = package.position(); entry.size = uncompressed; entry.crc32 = crc;
    if (!strcmp(entry.name, "manifest.json")) manifest = entry;
    if (entryCount < 4) entries[entryCount++] = entry;
    else { fail(error, errorSize, "ZIP contains unexpected extra entries"); return false; }
    if (!package.seek(entry.dataOffset + entry.size)) { fail(error, errorSize, "ZIP entry exceeds file"); return false; }
  }
  if (!manifest.size) { fail(error, errorSize, "manifest.json missing"); return false; }
  return true;
}

bool NrfLegacyDfuRelay::readEntry(File& package, const ZipEntry& entry, uint8_t* destination, size_t capacity) {
  return entry.size <= capacity && package.seek(entry.dataOffset) &&
         package.read(destination, entry.size) == static_cast<int>(entry.size);
}

bool NrfLegacyDfuRelay::verifyEntryCrc(File& package, const ZipEntry& entry) {
  if (!package.seek(entry.dataOffset)) return false;
  uint8_t buffer[256]; uint32_t remaining = entry.size, crc = 0xffffffffUL;
  while (remaining) {
    const size_t wanted = remaining > sizeof(buffer) ? sizeof(buffer) : remaining;
    const int count = package.read(buffer, wanted);
    if (count != static_cast<int>(wanted)) return false;
    crc = crc32Update(crc, buffer, wanted); remaining -= wanted;
  }
  return (crc ^ 0xffffffffUL) == entry.crc32;
}

bool NrfLegacyDfuRelay::validatePackage(fs::FS& fs, const char* path, PackageInfo& info,
                                        char* error, size_t errorSize) {
  info = PackageInfo{};
  File package = fs.open(path, FILE_READ);
  if (!package) { fail(error, errorSize, "package file not found"); return false; }
  ZipEntry manifest, entries[4]; size_t entryCount = 0;
  if (!locateEntries(package, manifest, entries, entryCount, error, errorSize) ||
      manifest.size >= 1024 || entryCount != 3) {
    if (!error || !error[0]) fail(error, errorSize, "package must contain manifest, BIN and DAT only");
    package.close(); return false;
  }
  uint8_t manifestText[1024] = {};
  if (!readEntry(package, manifest, manifestText, sizeof(manifestText) - 1) || !verifyEntryCrc(package, manifest)) {
    fail(error, errorSize, "manifest read or ZIP CRC failed"); package.close(); return false;
  }
  cJSON* root = cJSON_Parse(reinterpret_cast<char*>(manifestText));
  cJSON* body = root ? cJSON_GetObjectItemCaseSensitive(root, "manifest") : nullptr;
  cJSON* app = body ? cJSON_GetObjectItemCaseSensitive(body, "application") : nullptr;
  cJSON* version = body ? cJSON_GetObjectItemCaseSensitive(body, "dfu_version") : nullptr;
  cJSON* binName = app ? cJSON_GetObjectItemCaseSensitive(app, "bin_file") : nullptr;
  cJSON* datName = app ? cJSON_GetObjectItemCaseSensitive(app, "dat_file") : nullptr;
  cJSON* init = app ? cJSON_GetObjectItemCaseSensitive(app, "init_packet_data") : nullptr;
  uint32_t deviceType = 0, firmwareCrc = 0;
  cJSON* sdReq = init ? cJSON_GetObjectItemCaseSensitive(init, "softdevice_req") : nullptr;
  const bool shape = cJSON_IsObject(body) && cJSON_IsObject(app) && cJSON_IsNumber(version) &&
      version->valuedouble == 0.5 && cJSON_IsString(binName) && cJSON_IsString(datName) &&
      cJSON_IsObject(init) && jsonUInt(init, "device_type", deviceType) &&
      jsonUInt(init, "firmware_crc16", firmwareCrc) && cJSON_IsArray(sdReq) &&
      cJSON_GetArraySize(sdReq) == 1 && cJSON_IsNumber(cJSON_GetArrayItem(sdReq, 0)) &&
      static_cast<uint32_t>(cJSON_GetArrayItem(sdReq, 0)->valuedouble) == SOFTDEVICE_REQUIREMENT &&
      deviceType == DEVICE_TYPE && !cJSON_HasObjectItem(body, "bootloader") &&
      !cJSON_HasObjectItem(body, "softdevice") && !cJSON_HasObjectItem(body, "softdevice_bootloader");
  if (!shape || strlen(binName->valuestring) >= sizeof(info.binaryName) ||
      strlen(datName->valuestring) >= sizeof(info.datName)) {
    fail(error, errorSize, "manifest is not compatible application-only Legacy DFU 0.5");
    cJSON_Delete(root); package.close(); return false;
  }
  strcpy(info.binaryName, binName->valuestring); strcpy(info.datName, datName->valuestring);
  ZipEntry binary, dat;
  for (size_t i = 0; i < entryCount; ++i) {
    if (!strcmp(entries[i].name, info.binaryName)) binary = entries[i];
    if (!strcmp(entries[i].name, info.datName)) dat = entries[i];
  }
  cJSON_Delete(root);
  if (!binary.size || binary.size > MAX_APPLICATION_BYTES || dat.size != 14 ||
      !verifyEntryCrc(package, binary) || !verifyEntryCrc(package, dat)) {
    fail(error, errorSize, "BIN/DAT missing, oversized, or ZIP CRC failed"); package.close(); return false;
  }
  uint8_t datBytes[14];
  if (!readEntry(package, dat, datBytes, sizeof(datBytes)) || readLe16(datBytes) != DEVICE_TYPE ||
      readLe16(datBytes + 2) != 0xffff || readLe32(datBytes + 4) != 0xffffffffUL ||
      readLe16(datBytes + 8) != 1 || readLe16(datBytes + 10) != SOFTDEVICE_REQUIREMENT ||
      readLe16(datBytes + 12) != static_cast<uint16_t>(firmwareCrc)) {
    fail(error, errorSize, "DAT target or CRC metadata mismatch"); package.close(); return false;
  }
  if (!package.seek(binary.dataOffset)) { fail(error, errorSize, "BIN seek failed"); package.close(); return false; }
  uint8_t buffer[256]; uint32_t remaining = binary.size; uint16_t crc16 = 0xffff;
  while (remaining) {
    const size_t wanted = remaining > sizeof(buffer) ? sizeof(buffer) : remaining;
    if (package.read(buffer, wanted) != static_cast<int>(wanted)) {
      fail(error, errorSize, "BIN truncated"); package.close(); return false;
    }
    crc16 = crc16Update(crc16, buffer, wanted); remaining -= wanted;
  }
  package.close();
  if (crc16 != static_cast<uint16_t>(firmwareCrc)) { fail(error, errorSize, "BIN CRC16 mismatch"); return false; }
  info.binaryOffset = binary.dataOffset; info.binarySize = binary.size; info.binaryZipCrc = binary.crc32;
  info.datOffset = dat.dataOffset; info.datSize = dat.size; info.datZipCrc = dat.crc32;
  info.firmwareCrc16 = crc16;
  return true;
}

bool NrfLegacyDfuRelay::writeControl(const uint8_t* data, size_t length, char* error, size_t errorSize) {
  const bool connected = control_ && control_->getRemoteService() &&
                         control_->getRemoteService()->getClient()->isConnected();
  const bool result = connected && control_->writeValue(const_cast<uint8_t*>(data), length, true);
  if (logger_) logger_("nRF DFU control command=0x%02x bytes=%lu write_result=%u connected=%u",
                       data && length ? data[0] : 0, (unsigned long)length,
                       (unsigned)result, (unsigned)connected);
  if (!result) {
    if (error && errorSize) snprintf(error, errorSize,
      "DFU control write failed command=0x%02x connected=%u", data && length ? data[0] : 0,
      (unsigned)connected);
    return false;
  }
  return true;
}

bool NrfLegacyDfuRelay::waitResponse(uint8_t opcode, uint32_t timeoutMs, char* error, size_t errorSize) {
  Notification notification;
  const uint64_t deadline = millis() + timeoutMs;
  while (static_cast<int64_t>(deadline - millis()) > 0) {
    if (xQueueReceive(notifications_, &notification, pdMS_TO_TICKS(100)) == pdTRUE) {
      if (isResponse(notification, opcode)) return true;
      if (notification.length >= 3 && notification.data[0] == OP_RESPONSE && notification.data[1] == opcode) {
        fail(error, errorSize, "DFU target rejected request"); return false;
      }
    }
  }
  fail(error, errorSize, "DFU response timeout"); return false;
}

bool NrfLegacyDfuRelay::waitPrn(uint32_t bytes, uint32_t timeoutMs, char* error, size_t errorSize) {
  Notification notification;
  const uint64_t deadline = millis() + timeoutMs;
  while (static_cast<int64_t>(deadline - millis()) > 0) {
    if (xQueueReceive(notifications_, &notification, pdMS_TO_TICKS(100)) == pdTRUE && isPrn(notification, bytes)) return true;
  }
  fail(error, errorSize, "DFU packet receipt timeout"); return false;
}

bool NrfLegacyDfuRelay::findBootloader(BLEClient* client, const BLEAddress& applicationAddress,
                                       char* error, size_t errorSize) {
  delete bootloaderDevice_; bootloaderDevice_ = nullptr;
  applicationAddress_ = applicationAddress.toString();
  DfuScanCallbacks callbacks(bootloaderDevice_, applicationAddress_);
  BLEScan* scan = BLEDevice::getScan();
  scan->setAdvertisedDeviceCallbacks(&callbacks); scan->setActiveScan(true);
  for (uint8_t attempt = 0; attempt < 10 && !bootloaderDevice_; ++attempt) {
    scan->start(2, false); scan->clearResults();
  }
  if (!bootloaderDevice_) { fail(error, errorSize, "matching DFU bootloader not found"); return false; }
  if (logger_) logger_("nRF DFU bootloader advertisement found address=%s",
                       bootloaderDevice_->getAddress().toString().c_str());
  if (!client->connectTimeout(bootloaderDevice_, 10000)) { fail(error, errorSize, "DFU bootloader connect failed"); return false; }
  if (logger_) logger_("nRF DFU bootloader connected address=%s mtu=%u",
                       client->getPeerAddress().toString().c_str(), (unsigned)client->getMTU());
  // Core 3.3.12 reports the negotiated connection MTU. Legacy DFU remains on
  // conservative 20-byte packets even when it is larger; do not trigger a
  // second exchange against the old bootloader.
  if (!mtuSupportsLegacyPacket(client->getMTU())) {
    fail(error, errorSize, "DFU bootloader MTU cannot carry 20-byte packets"); return false;
  }
  BLERemoteService* service = client->getService(kDfuService);
  control_ = service ? service->getCharacteristic(kDfuControl) : nullptr;
  packet_ = service ? service->getCharacteristic(kDfuPacket) : nullptr;
  if (!control_ || !packet_ || !control_->canNotify() || !control_->canWrite() || !packet_->canWriteNoResponse()) {
    fail(error, errorSize, "DFU characteristics unavailable"); return false;
  }
  if (logger_) logger_("nRF DFU service discovered control notify=%u write=%u write_no_response=%u packet_write_no_response=%u",
                       (unsigned)control_->canNotify(), (unsigned)control_->canWrite(),
                       (unsigned)control_->canWriteNoResponse(), (unsigned)packet_->canWriteNoResponse());
  if (logger_) logger_("nRF DFU transfer parameters negotiated_mtu=%u payload=%u PRN=%u",
                       (unsigned)client->getMTU(), (unsigned)PACKET_BYTES, (unsigned)PRN_INTERVAL);
  control_->registerForNotify(notifyCallback);
  return true;
}

bool NrfLegacyDfuRelay::streamBinary(File& package, const PackageInfo& info,
                                     volatile bool* cancelRequested, char* error, size_t errorSize,
                                     ProgressFunction progress, void* progressContext) {
  if (!package.seek(info.binaryOffset)) { fail(error, errorSize, "BIN seek failed"); return false; }
  uint8_t bytes[PACKET_BYTES]; uint32_t sent = 0; uint16_t packetsSincePrn = 0;
  while (sent < info.binarySize) {
    if (cancelRequested && *cancelRequested) { fail(error, errorSize, "DFU cancelled"); return false; }
    const size_t wanted = info.binarySize - sent > sizeof(bytes) ? sizeof(bytes) : info.binarySize - sent;
    if (package.read(bytes, wanted) != static_cast<int>(wanted) || !packet_->writeValue(bytes, wanted, false)) {
      fail(error, errorSize, "DFU packet write failed"); return false;
    }
    sent += wanted;
    if (progress) progress(sent, info.binarySize, progressContext);
    if (++packetsSincePrn == PRN_INTERVAL) {
      if (!waitPrn(sent, 10000, error, errorSize)) return false;
      packetsSincePrn = 0;
    }
    if (logger_ && (sent == info.binarySize || sent % 16384 < PACKET_BYTES))
      logger_("nRF DFU progress bytes=%lu/%lu", (unsigned long)sent, (unsigned long)info.binarySize);
  }
  return true;
}

bool NrfLegacyDfuRelay::run(fs::FS& fs, const char* path, const PackageInfo& info, BLEClient* client,
                            const BLEAddress& applicationAddress, volatile bool* cancelRequested,
                            char* error, size_t errorSize, ProgressFunction progress,
                            void* progressContext) {
  if (!client || !client->isConnected()) { fail(error, errorSize, "sensor application is not connected"); return false; }
  notifications_ = xQueueCreate(8, sizeof(Notification)); active_ = this;
  if (!notifications_) { fail(error, errorSize, "DFU notification queue allocation failed"); active_ = nullptr; return false; }
  bool ok = false;
  do {
    if (!mtuSupportsLegacyPacket(client->getMTU())) {
      fail(error, errorSize, "application MTU cannot carry 20-byte DFU command packets"); break;
    }
    BLERemoteService* appService = client->getService(kDfuService);
    control_ = appService ? appService->getCharacteristic(kDfuControl) : nullptr;
    if (!control_ || !control_->canNotify() || !control_->canWrite()) { fail(error, errorSize, "application DFU service unavailable"); break; }
    control_->registerForNotify(notifyCallback);
    uint8_t enter = OP_START_DFU;
    const bool entryWrite = control_->writeValue(&enter, 1, true);
    if (logger_) logger_("nRF DFU bootloader entry requested command=0x%02x write_result=%u",
                         (unsigned)enter, (unsigned)entryWrite);
    for (uint8_t wait = 0; wait < 50 && client->isConnected(); ++wait) vTaskDelay(pdMS_TO_TICKS(100));
    if (!resetTransitionAccepted(entryWrite, client->isConnected())) {
      fail(error, errorSize, entryWrite ? "application did not enter bootloader" :
           "application DFU entry write failed without disconnect"); break;
    }
    if (logger_) logger_("nRF DFU application disconnected (expected); scanning for bootloader");
    vTaskDelay(pdMS_TO_TICKS(500));
    if (!findBootloader(client, applicationAddress, error, errorSize)) break;

    uint8_t start[2] = {OP_START_DFU, IMAGE_TYPE_APPLICATION};
    uint8_t sizes[12] = {}; writeLe32(sizes + 8, info.binarySize);
    if (!writeControl(start, sizeof(start), error, errorSize) ||
        !packet_->writeValue(sizes, sizeof(sizes), false) ||
        !waitResponse(OP_START_DFU, 10000, error, errorSize)) break;

    File package = fs.open(path, FILE_READ);
    uint8_t dat[14];
    if (!package || !package.seek(info.datOffset) || package.read(dat, sizeof(dat)) != sizeof(dat)) {
      fail(error, errorSize, "DAT read failed"); if (package) package.close(); break;
    }
    uint8_t initStart[2] = {OP_INIT_DFU, 0};
    uint8_t initComplete[2] = {OP_INIT_DFU, 1};
    if (!writeControl(initStart, sizeof(initStart), error, errorSize) ||
        !packet_->writeValue(dat, sizeof(dat), false) ||
        !writeControl(initComplete, sizeof(initComplete), error, errorSize) ||
        !waitResponse(OP_INIT_DFU, 10000, error, errorSize)) { package.close(); break; }

    uint8_t prn[3] = {OP_PACKET_RECEIPT_NOTIFICATION_REQUEST,
                      static_cast<uint8_t>(PRN_INTERVAL), static_cast<uint8_t>(PRN_INTERVAL >> 8)};
    uint8_t receive = OP_RECEIVE_FIRMWARE;
    if (!writeControl(prn, sizeof(prn), error, errorSize) ||
        !writeControl(&receive, 1, error, errorSize) ||
        !streamBinary(package, info, cancelRequested, error, errorSize, progress, progressContext) ||
        !waitResponse(OP_RECEIVE_FIRMWARE, 15000, error, errorSize)) { package.close(); break; }
    package.close();

    uint8_t validate = OP_VALIDATE_FIRMWARE;
    uint8_t activate = OP_ACTIVATE_AND_RESET;
    if (!writeControl(&validate, 1, error, errorSize) ||
        !waitResponse(OP_VALIDATE_FIRMWARE, 15000, error, errorSize)) break;
    const bool activateWrite = control_->writeValue(&activate, 1, true);
    if (logger_) logger_("nRF DFU activate-and-reset requested command=0x%02x write_result=%u",
                         (unsigned)activate, (unsigned)activateWrite);
    for (uint8_t wait = 0; wait < 50 && client->isConnected(); ++wait) vTaskDelay(pdMS_TO_TICKS(100));
    if (!resetTransitionAccepted(activateWrite, client->isConnected())) {
      fail(error, errorSize, activateWrite ? "bootloader did not reset after activation" :
           "activate-and-reset write failed without disconnect"); break;
    }
    ok = true;
  } while (false);

  if (!ok && client && client->isConnected() && control_) {
    uint8_t reset = OP_RESET; control_->writeValue(&reset, 1, true);
  }
  if (client && client->isConnected()) client->disconnect();
  active_ = nullptr; vQueueDelete(notifications_); notifications_ = nullptr;
  control_ = nullptr; packet_ = nullptr; delete bootloaderDevice_; bootloaderDevice_ = nullptr;
  return ok;
}
