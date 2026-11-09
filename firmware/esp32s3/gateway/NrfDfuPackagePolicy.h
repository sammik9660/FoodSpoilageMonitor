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

// Only while recording is STOPPED, all pending samples are drained and BLE
// ingress is paused. Both ZIPs must fit during download, and the FULL normal
// spool reserve must fit again after the old ZIP is removed on promotion.
inline size_t pausedIngressCapacity(size_t total,size_t used,size_t oldZipBytes,size_t spoolReserve) {
  if(oldZipBytes>used||used>=total||total-used<=FILESYSTEM_HEADROOM_BYTES)return 0;
  const size_t overlap=total-used-FILESYSTEM_HEADROOM_BYTES;
  const size_t afterPromotion=temporaryCapacity(total,used-oldZipBytes,spoolReserve);
  return overlap<afterPromotion?overlap:afterPromotion;
}

}  // namespace nrf_dfu_package
