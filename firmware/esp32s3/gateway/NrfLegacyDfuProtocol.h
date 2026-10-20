#pragma once

#include <stddef.h>
#include <stdint.h>

namespace nrf_legacy_dfu {

constexpr uint8_t OP_START_DFU = 0x01;
constexpr uint8_t OP_INIT_DFU = 0x02;
constexpr uint8_t OP_RECEIVE_FIRMWARE = 0x03;
constexpr uint8_t OP_VALIDATE_FIRMWARE = 0x04;
constexpr uint8_t OP_ACTIVATE_AND_RESET = 0x05;
constexpr uint8_t OP_RESET = 0x06;
constexpr uint8_t OP_PACKET_RECEIPT_NOTIFICATION_REQUEST = 0x08;
constexpr uint8_t OP_RESPONSE = 0x10;
constexpr uint8_t OP_PACKET_RECEIPT_NOTIFICATION = 0x11;
constexpr uint8_t RESPONSE_SUCCESS = 0x01;
constexpr uint8_t IMAGE_TYPE_APPLICATION = 0x04;
constexpr uint16_t ATT_MTU = 23;
constexpr uint16_t PACKET_BYTES = 20;
constexpr uint16_t PRN_INTERVAL = 8;
constexpr uint16_t DEVICE_TYPE = 0x0052;
constexpr uint16_t SOFTDEVICE_REQUIREMENT = 0x00B6;
constexpr uint32_t MAX_APPLICATION_BYTES = 815104;

inline bool mtuSupportsLegacyPacket(uint16_t negotiatedMtu) {
  return negotiatedMtu >= ATT_MTU && PACKET_BYTES <= negotiatedMtu - 3;
}

struct Notification {
  uint8_t data[20];
  uint8_t length;
};

inline uint16_t readLe16(const uint8_t* value) {
  return static_cast<uint16_t>(value[0]) |
         (static_cast<uint16_t>(value[1]) << 8);
}

inline uint32_t readLe32(const uint8_t* value) {
  return static_cast<uint32_t>(value[0]) |
         (static_cast<uint32_t>(value[1]) << 8) |
         (static_cast<uint32_t>(value[2]) << 16) |
         (static_cast<uint32_t>(value[3]) << 24);
}

inline void writeLe32(uint8_t* value, uint32_t number) {
  value[0] = static_cast<uint8_t>(number);
  value[1] = static_cast<uint8_t>(number >> 8);
  value[2] = static_cast<uint8_t>(number >> 16);
  value[3] = static_cast<uint8_t>(number >> 24);
}

inline uint16_t crc16Update(uint16_t crc, const uint8_t* data, size_t length) {
  while (length--) {
    crc = static_cast<uint16_t>((crc >> 8) | (crc << 8));
    crc ^= *data++;
    crc ^= static_cast<uint8_t>(crc & 0xff) >> 4;
    crc ^= static_cast<uint16_t>(crc << 12);
    crc ^= static_cast<uint16_t>((crc & 0xff) << 5);
  }
  return crc;
}

inline uint32_t crc32Update(uint32_t crc, const uint8_t* data, size_t length) {
  while (length--) {
    crc ^= *data++;
    for (uint8_t bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ (0xEDB88320UL & (0U - (crc & 1U)));
  }
  return crc;
}

inline bool isResponse(const Notification& notification, uint8_t requestOpcode) {
  return notification.length >= 3 && notification.data[0] == OP_RESPONSE &&
         notification.data[1] == requestOpcode && notification.data[2] == RESPONSE_SUCCESS;
}

inline bool isPrn(const Notification& notification, uint32_t expectedBytes) {
  return notification.length >= 5 &&
         notification.data[0] == OP_PACKET_RECEIPT_NOTIFICATION &&
         readLe32(notification.data + 1) == expectedBytes;
}

// Legacy application entry and ACTIVATE_AND_RESET may drop the BLE link before
// a write-with-response completion reaches the central. The transition is
// accepted only when the expected disconnect is actually observed; a false
// write result while the link remains up is still a failure.
inline bool resetTransitionAccepted(bool writeResult, bool connectedAfterWait) {
  (void)writeResult;
  return !connectedAfterWait;
}

}  // namespace nrf_legacy_dfu
