#include <cassert>
#include <iostream>

#include "../firmware/esp32s3/gateway/NrfDfuPackagePolicy.h"

using namespace nrf_dfu_package;

int main() {
  assert(temporaryCapacity(917504, 0, 550502) == 917504 - 550502 - 8192);
  assert(temporaryCapacity(917504, 162102, 550502) >= 162102);
  assert(temporaryCapacity(917504, 400000, 550502) == 0);
  assert(temporaryCapacity(100, 100, 0) == 0);
  assert(temporaryCapacity(2000000, 0, 0) == MAX_PACKAGE_BYTES);
  assert(temporaryCapacity(836081, 177274, 501648) < 177274);
  assert(pausedIngressCapacity(836081, 177274, 177274, 501648) >= 177274);
  assert(pausedIngressCapacity(836081, 177274, 0, 501648) < 177274); // no old ZIP to reclaim
  assert(pausedIngressCapacity(836081, 660000, 177274, 501648) < 177274); // overlap cannot fit
  assert(pausedIngressCapacity(836081, 177274, 177274, 600000) < 300000); // full reserve must return
  assert(pausedIngressCapacity(100, 100, 0, 501648) == 0);
  std::cout << "nrf dfu package policy tests passed\n";
}
