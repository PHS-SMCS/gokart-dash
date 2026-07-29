// CRSF (Crossfire / ExpressLRS) receiver decoder — Teensy side of the RC link.
//
// Pure C++ (no Arduino) so it is host-tested under `pio test -e native`. Ported
// from the proven legacy kart_controller sketch (serviceTransceiver /
// processCrsfFrame / unpackRcChannels): sync on the CRSF address byte, bound and
// CRC8-check each frame, then unpack the 16 packed 11-bit RC channels and the
// LINK_STATISTICS (RSSI / LQ / SNR).
//
// Bytes are fed one at a time to match a UART drain loop. The receiver streams
// RC_CHANNELS *continuously* (not on change), so link_up() timing out is the
// dead-man signal: no fresh frame within link_timeout_ms => the link is down =>
// the caller forces PARK. (Configure the ELRS RX failsafe to "No Pulses" so a
// lost link actually stops the stream instead of holding the last values.)
#pragma once

#include <stdint.h>

namespace kart {

// CRSF frame CRC8 (poly 0xD5) over the type+payload bytes. Free function so the
// decoder and its host tests share one definition.
inline uint8_t crsf_crc8(const uint8_t *data, uint8_t len) {
  static constexpr uint8_t kPoly = 0xD5;
  uint8_t crc = 0;
  for (uint8_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t j = 0; j < 8; j++) {
      crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ kPoly) : (uint8_t)(crc << 1);
    }
  }
  return crc;
}

struct RcLinkConfig {
  uint32_t link_timeout_ms = 500;  // no valid frame this long => link down
  uint16_t crsf_min = 172;         // stick/switch low endpoint
  uint16_t crsf_max = 1811;        // stick/switch high endpoint
  uint16_t switch_on = 1000;       // >= this => a 2-position switch is "on"
};

class RcLink {
 public:
  static constexpr uint8_t kChannelCount = 16;

  explicit RcLink(const RcLinkConfig &cfg = RcLinkConfig()) : cfg_(cfg) {}

  // Feed one received byte. `now_ms` timestamps a completed *valid* frame.
  void feed(uint8_t b, uint32_t now_ms);

  // A valid RC_CHANNELS frame arrived within the link timeout.
  bool link_up(uint32_t now_ms) const {
    return frame_count_ > 0 &&
           (uint32_t)(now_ms - last_frame_ms_) < cfg_.link_timeout_ms;
  }

  uint16_t channel(uint8_t i) const {
    return (i < kChannelCount) ? channels_[i] : 0;
  }

  // CRSF channel (crsf_min..crsf_max) -> 0..100 %, clamped.
  float channel_pct(uint8_t i) const {
    int32_t v = channel(i);
    if (v <= cfg_.crsf_min) return 0.0f;
    if (v >= cfg_.crsf_max) return 100.0f;
    return (float)(v - cfg_.crsf_min) * 100.0f / (float)(cfg_.crsf_max - cfg_.crsf_min);
  }

  // CRSF channel -> signed axis (-scale..+scale), center (crsf_mid) = 0. Lets a
  // steering channel reuse SteerLink::axis_to_setpoint (which wants a ±full axis).
  int32_t channel_axis(uint8_t i, int32_t scale = 32767) const {
    int32_t mid = ((int32_t)cfg_.crsf_min + (int32_t)cfg_.crsf_max) / 2;
    int32_t half = ((int32_t)cfg_.crsf_max - (int32_t)cfg_.crsf_min) / 2;
    if (half == 0) return 0;
    int32_t v = (int32_t)channel(i) - mid;
    if (v > half) v = half;
    if (v < -half) v = -half;
    return v * scale / half;
  }

  // 2-position switch decode. Endpoints (172 / 1811) sit far from the threshold,
  // so a single level is chatter-free without hysteresis; edge detection is the
  // caller's job (compare successive ticks).
  bool switch_high(uint8_t i) const { return channel(i) >= cfg_.switch_on; }

  // Diagnostics (RX? command).
  uint32_t frame_count() const { return frame_count_; }
  uint32_t bad_crc_count() const { return bad_crc_count_; }
  uint32_t last_frame_ms() const { return last_frame_ms_; }
  uint8_t rssi1() const { return rssi1_; }
  uint8_t rssi2() const { return rssi2_; }
  uint8_t lq() const { return lq_; }
  int8_t snr() const { return snr_; }

 private:
  void process_frame(uint8_t total_len, uint32_t now_ms);
  void unpack_channels(const uint8_t *p);

  static constexpr uint8_t kSyncByte = 0xC8;
  static constexpr uint8_t kMaxFrame = 64;
  static constexpr uint8_t kTypeRcChannels = 0x16;
  static constexpr uint8_t kTypeLinkStats = 0x14;

  RcLinkConfig cfg_;
  uint8_t buf_[kMaxFrame];
  uint8_t len_ = 0;

  uint16_t channels_[kChannelCount] = {0};
  uint32_t frame_count_ = 0;
  uint32_t bad_crc_count_ = 0;
  uint32_t last_frame_ms_ = 0;
  uint8_t rssi1_ = 0, rssi2_ = 0, lq_ = 0;
  int8_t snr_ = 0;
};

}  // namespace kart
