#pragma once

#include "prism/usb/raw_header.hpp"
#include <cstdint>
#include <cstring>
#include <optional>

namespace prism_viewer {
struct ProfileFrameMetadata {
  uint8_t timing_mode = 0;
  uint64_t timestamp_us = 0;
  uint32_t source_frame = 0, exposure_us = 0, gain_x1024 = 0;
  uint32_t trigger_sequence = 0;
};

// The index reader may supply just the prefix. Validate lengths against the
// complete encoded image, and validate the APP15 CRC before using its fields.
inline std::optional<ProfileFrameMetadata> profileJpegMetadata(
    const uint8_t* bytes, size_t available, size_t encoded_size, unsigned camera) {
  using prism::capture::rawHeaderLe32;
  if (!bytes || available < 70 || encoded_size < 70 ||
      std::memcmp(bytes, "\xff\xd8\xff\xef\x00\x42", 6) != 0) return {};
  const auto* p = bytes + 6;
  const bool trigger = p[4] == 2;
  const size_t crc_offset = trigger ? 60 : 48;
  if (std::memcmp(p, "PRJ1", 4) || (!trigger && p[4] != 1) || p[5] != 64 ||
      p[28] != camera || p[29] != 1 || rawHeaderLe32(p+8) != 1280 || rawHeaderLe32(p+12) != 1024 ||
      rawHeaderLe32(p+44) != encoded_size - 68 ||
      rawHeaderLe32(p+crc_offset) != prism::capture::rawHeaderCrc32(p,crc_offset)) return {};
  return ProfileFrameMetadata{p[30], rawHeaderLe32(p+32) | (uint64_t(rawHeaderLe32(p+36)) << 32),
      rawHeaderLe32(p+16), rawHeaderLe32(p+20), rawHeaderLe32(p+24), trigger ? rawHeaderLe32(p+48) : 0};
}
}  // namespace prism_viewer
