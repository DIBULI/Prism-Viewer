#pragma once
#include "grouped_preview_mailbox.hpp"
#include "prism/usb/profile_video.hpp"
#include <cstring>

namespace prism_viewer::ui {
// Scheduling hint only. Never display this metadata or pixels before the
// existing full RAW decoder has validated BOTH CRCs and camera identity.
inline std::optional<PreviewGroupKey> rawPreviewGroupHint(
    const std::vector<uint8_t>& bytes, unsigned camera, uint8_t format) {
  const bool rice = format == prism::capture::kRaw12Rice;
  if (!rice && format != prism::capture::kRaw12Zstd) return {};
  try {
    const auto h=prism::capture::parseRaw12Header(bytes.data(),bytes.size(),bytes.size(),format);
    const auto* p=bytes.data();
    if(!h.trigger_sequence||p[37]!=camera||p[38]!=4)return {};
    const uint64_t timestamp=uint64_t(prism::capture::rawLe32(p+40)) |
        uint64_t(prism::capture::rawLe32(p+44))<<32;
    return PreviewGroupKey{h.trigger_sequence,timestamp,true};
  } catch(const std::exception&) {return {};}
}
}
