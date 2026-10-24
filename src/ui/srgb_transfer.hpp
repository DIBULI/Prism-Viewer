#ifndef PRISM_PREVIEW_SRGB_TRANSFER_HPP
#define PRISM_PREVIEW_SRGB_TRANSFER_HPP
#include <array>
#include <cmath>
#include <cstdint>

namespace prism_preview {
// IEC 61966-2-1 sRGB encoding of a linear monochrome preview.
// Apply once, AFTER linear CFA filtering/resizing, never to RAW or AE inputs.
// Kept identical in Web and Viewer; PTN1 wire samples remain linear8.
inline const std::array<uint8_t, 256>& linear8ToSrgb() {
  static const auto table = [] {
    std::array<uint8_t, 256> result{};
    for (unsigned i = 0; i < result.size(); ++i) {
      const double x = double(i) / 255.0;
      const double encoded = x <= 0.0031308
          ? 12.92 * x : 1.055 * std::pow(x, 1.0 / 2.4) - 0.055;
      result[i] = static_cast<uint8_t>(std::lround(encoded * 255.0));
    }
    return result;
  }();
  return table;
}
}  // namespace prism_preview
#endif
