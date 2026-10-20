#include <cassert>

#include "../firmware/esp32s3/gateway/WifiRecoveryPolicy.h"

int main() {
  assert(wifi_recovery::preferredSlot() == 0);
  assert(wifi_recovery::nextSlot(0, 2) == 1);
  assert(wifi_recovery::nextSlot(1, 2) == 0);
  assert(wifi_recovery::nextSlot(0, 0) == 0);
  assert(!wifi_recovery::cycleComplete(1, 2));
  assert(wifi_recovery::cycleComplete(2, 2));
  return 0;
}
