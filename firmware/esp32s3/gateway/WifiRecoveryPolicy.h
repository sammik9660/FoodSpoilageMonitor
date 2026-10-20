#pragma once

#include <stdint.h>

namespace wifi_recovery {

inline uint8_t nextSlot(uint8_t current, uint8_t count) {
  return count ? static_cast<uint8_t>((current + 1) % count) : 0;
}

inline bool cycleComplete(uint8_t attempts, uint8_t count) {
  return count && attempts >= count;
}

// Every new outage and every completed retry cycle starts with the preferred
// hotspot in slot 0. A healthy connection is handled outside this policy and
// is never switched merely to reach the preferred slot.
inline uint8_t preferredSlot() { return 0; }

}  // namespace wifi_recovery
