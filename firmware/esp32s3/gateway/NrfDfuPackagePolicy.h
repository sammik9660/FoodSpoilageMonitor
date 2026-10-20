#pragma once

#include <stddef.h>

namespace nrf_dfu_package {

constexpr char FINAL_PATH[] = "/nrf-dfu.zip";
constexpr char TEMP_PATH[] = "/nrf-dfu.upload.tmp";
constexpr char BACKUP_PATH[] = "/nrf-dfu.backup";
constexpr size_t FILESYSTEM_HEADROOM_BYTES = 8192;
constexpr size_t MAX_PACKAGE_BYTES = 815104 + 16384;

inline size_t temporaryCapacity(size_t total, size_t used, size_t spoolReserve) {
  if (used >= total) return 0;
  const size_t freeBytes = total - used;
  if (freeBytes <= spoolReserve + FILESYSTEM_HEADROOM_BYTES) return 0;
  const size_t available = freeBytes - spoolReserve - FILESYSTEM_HEADROOM_BYTES;
  return available < MAX_PACKAGE_BYTES ? available : MAX_PACKAGE_BYTES;
}

}  // namespace nrf_dfu_package
