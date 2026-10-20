#include <cassert>

#include "../firmware/esp32s3/gateway/WifiRecoveryPolicy.h"

int main() {
  assert(wifi_recovery::preferredSlot(1, 2) == 1);
  assert(wifi_recovery::preferredSlot(2, 2) == 0);
  assert(wifi_recovery::preferredSlot(0xff, 2) == 0);
  assert(wifi_recovery::preferredSlot(0xff, 0) == 0);
  assert(wifi_recovery::nextSlot(0, 2) == 1);
  assert(wifi_recovery::nextSlot(1, 2) == 0);
  assert(wifi_recovery::nextSlot(0, 0) == 0);
  assert(!wifi_recovery::cycleComplete(1, 2));
  assert(wifi_recovery::cycleComplete(2, 2));
  assert(!wifi_recovery::disconnectComplete(false, wifi_recovery::DISCONNECT_EVENT_FALLBACK_MS - 1));
  assert(wifi_recovery::disconnectComplete(true, 0));
  assert(wifi_recovery::disconnectComplete(false, wifi_recovery::DISCONNECT_EVENT_FALLBACK_MS));
  assert(wifi_recovery::staleLocalLeave(false, 8));
  assert(!wifi_recovery::staleLocalLeave(true, 8));
  assert(!wifi_recovery::staleLocalLeave(false, 201));
  assert(wifi_recovery::CONNECT_TIMEOUT_MS < 20000);
  assert(wifi_recovery::RETRY_CYCLE_MS < 30000);
  return 0;
}
