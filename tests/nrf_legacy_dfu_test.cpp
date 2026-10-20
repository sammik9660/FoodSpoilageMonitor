#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>

#include "../firmware/esp32s3/gateway/NrfLegacyDfuProtocol.h"

using namespace nrf_legacy_dfu;

int main() {
  uint8_t value[4];
  writeLe32(value, 161240);
  assert(readLe32(value) == 161240);

  Notification response = {{OP_RESPONSE, OP_VALIDATE_FIRMWARE, RESPONSE_SUCCESS}, 3};
  assert(isResponse(response, OP_VALIDATE_FIRMWARE));
  assert(!isResponse(response, OP_RECEIVE_FIRMWARE));

  Notification prn = {};
  prn.length = 5; prn.data[0] = OP_PACKET_RECEIPT_NOTIFICATION;
  writeLe32(prn.data + 1, 160);
  assert(isPrn(prn, 160));
  assert(!isPrn(prn, 140));

  const uint8_t test[] = "123456789";
  assert(crc16Update(0xffff, test, 9) == 0x29b1);
  assert((crc32Update(0xffffffffUL, test, 9) ^ 0xffffffffUL) == 0xcbf43926UL);

  assert(ATT_MTU == 23 && PACKET_BYTES == 20 && PRN_INTERVAL == 8);
  assert(!mtuSupportsLegacyPacket(0));
  assert(!mtuSupportsLegacyPacket(22));
  assert(mtuSupportsLegacyPacket(23));
  assert(mtuSupportsLegacyPacket(247));
  assert(mtuSupportsLegacyPacket(517));
  assert(DEVICE_TYPE == 0x0052 && SOFTDEVICE_REQUIREMENT == 0x00b6);
  assert(resetTransitionAccepted(true, false));
  assert(resetTransitionAccepted(false, false));  // response lost during expected reset
  assert(!resetTransitionAccepted(false, true));
  assert(!resetTransitionAccepted(true, true));
  std::cout << "nrf legacy dfu protocol tests passed\n";
}
