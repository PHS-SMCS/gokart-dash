#include "rc_link.h"

namespace kart {

// Byte-at-a-time CRSF frame assembler. Frame layout:
//   <sync=0xC8> <len> <type> <payload...> <crc8>
// where `len` counts every byte after itself (type + payload + crc), so the
// full on-wire length is len + 2. Mirrors the legacy serviceTransceiver().
void RcLink::feed(uint8_t b, uint32_t now_ms) {
  if (len_ == 0) {
    if (b == kSyncByte) buf_[len_++] = b;
    return;
  }

  if (len_ < kMaxFrame) {
    buf_[len_++] = b;
  } else {
    len_ = 0;  // overrun: drop and resync
    return;
  }

  if (len_ == 2) {
    uint8_t len_field = buf_[1];
    if (len_field < 2 || len_field > kMaxFrame - 2) len_ = 0;  // bogus length
    return;
  }

  if (len_ >= 2) {
    uint8_t total_len = (uint8_t)(buf_[1] + 2);
    if (len_ >= total_len) {
      process_frame(total_len, now_ms);
      len_ = 0;
    }
  }
}

void RcLink::process_frame(uint8_t total_len, uint32_t now_ms) {
  // buf_ is <sync><len><type><payload...><crc>; the CRC covers type+payload,
  // i.e. bytes [2 .. total_len-2].
  if (total_len < 4) return;
  uint8_t crc_got = buf_[total_len - 1];
  uint8_t crc_calc = crsf_crc8(&buf_[2], (uint8_t)(total_len - 3));
  if (crc_got != crc_calc) {
    bad_crc_count_++;
    return;
  }

  frame_count_++;
  last_frame_ms_ = now_ms;

  uint8_t type = buf_[2];
  if (type == kTypeRcChannels && total_len == 26) {
    unpack_channels(&buf_[3]);
  } else if (type == kTypeLinkStats && total_len >= 12) {
    rssi1_ = buf_[3];
    rssi2_ = buf_[4];
    lq_ = buf_[5];
    snr_ = (int8_t)buf_[6];
  }
}

// 16 channels × 11 bits, little-endian bit packing (CRSF RC_CHANNELS_PACKED).
void RcLink::unpack_channels(const uint8_t *p) {
  channels_[0]  = (uint16_t)(( p[0]       | p[1]  << 8)                    & 0x07FF);
  channels_[1]  = (uint16_t)(( p[1]  >> 3 | p[2]  << 5)                    & 0x07FF);
  channels_[2]  = (uint16_t)(( p[2]  >> 6 | p[3]  << 2 | p[4]  << 10)      & 0x07FF);
  channels_[3]  = (uint16_t)(( p[4]  >> 1 | p[5]  << 7)                    & 0x07FF);
  channels_[4]  = (uint16_t)(( p[5]  >> 4 | p[6]  << 4)                    & 0x07FF);
  channels_[5]  = (uint16_t)(( p[6]  >> 7 | p[7]  << 1 | p[8]  << 9)       & 0x07FF);
  channels_[6]  = (uint16_t)(( p[8]  >> 2 | p[9]  << 6)                    & 0x07FF);
  channels_[7]  = (uint16_t)(( p[9]  >> 5 | p[10] << 3)                    & 0x07FF);
  channels_[8]  = (uint16_t)(( p[11]      | p[12] << 8)                    & 0x07FF);
  channels_[9]  = (uint16_t)(( p[12] >> 3 | p[13] << 5)                    & 0x07FF);
  channels_[10] = (uint16_t)(( p[13] >> 6 | p[14] << 2 | p[15] << 10)      & 0x07FF);
  channels_[11] = (uint16_t)(( p[15] >> 1 | p[16] << 7)                    & 0x07FF);
  channels_[12] = (uint16_t)(( p[16] >> 4 | p[17] << 4)                    & 0x07FF);
  channels_[13] = (uint16_t)(( p[17] >> 7 | p[18] << 1 | p[19] << 9)       & 0x07FF);
  channels_[14] = (uint16_t)(( p[19] >> 2 | p[20] << 6)                    & 0x07FF);
  channels_[15] = (uint16_t)(( p[20] >> 5 | p[21] << 3)                    & 0x07FF);
}

}  // namespace kart
