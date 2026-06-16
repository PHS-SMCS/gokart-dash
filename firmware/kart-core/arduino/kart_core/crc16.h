// CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflection, no final XOR).
// Used by the UART telemetry framing — see docs/protocols/uart-protocol.md.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace kart {

inline uint16_t crc16_ccitt_false(const uint8_t *data, size_t len,
                                  uint16_t crc = 0xFFFF) {
  for (size_t i = 0; i < len; i++) {
    crc ^= (uint16_t)data[i] << 8;
    for (int bit = 0; bit < 8; bit++) {
      crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021)
                           : (uint16_t)(crc << 1);
    }
  }
  return crc;
}

}  // namespace kart
