#pragma once

#include <stdint.h>

namespace wifi_recovery {

constexpr uint32_t CONNECT_TIMEOUT_MS = 12000;
constexpr uint32_t RETRY_CYCLE_MS = 10000;
constexpr uint32_t SLOT_SETTLE_MS = 250;
constexpr uint32_t DISCONNECT_EVENT_FALLBACK_MS = 2000;
constexpr uint8_t LOCAL_ASSOC_LEAVE_REASON = 8;

inline uint8_t nextSlot(uint8_t current, uint8_t count) {
  return count ? static_cast<uint8_t>((current + 1) % count) : 0;
}

inline bool cycleComplete(uint8_t attempts, uint8_t count) {
  return count && attempts >= count;
}

// Every new outage and completed retry cycle starts with the last successful
// registered slot. Invalid or missing history safely falls back to slot 0. A
// healthy connection is handled outside this policy and is never switched.
inline uint8_t preferredSlot(uint8_t lastSuccessful, uint8_t count) {
  return lastSuccessful < count ? lastSuccessful : 0;
}

inline bool disconnectComplete(bool eventReceived, uint32_t elapsedMs) {
  return eventReceived || elapsedMs >= DISCONNECT_EVENT_FALLBACK_MS;
}

// ESP-IDF reason 8 is emitted when this station leaves an association. It can
// arrive after the next slot attempt has begun, so it must not fail that new
// attempt unless serviceWifi() is still waiting for its own disconnect.
inline bool staleLocalLeave(bool disconnectPending, uint8_t reason) {
  return !disconnectPending && reason == LOCAL_ASSOC_LEAVE_REASON;
}

}  // namespace wifi_recovery
