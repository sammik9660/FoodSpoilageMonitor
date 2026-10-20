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
  std::cout << "nrf dfu package policy tests passed\n";
}
