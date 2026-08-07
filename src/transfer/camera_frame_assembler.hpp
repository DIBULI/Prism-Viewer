#pragma once

#include "prism/usb_sdk.hpp"

#include <array>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace prism_viewer::transfer {

struct CameraFrameSet {
  uint32_t frame_id = 0;
  uint64_t timestamp_us = 0;
  std::array<std::vector<uint8_t>, 4> jpeg;
  prism::VideoMeta metadata;
};

struct CameraChunkResult {
  std::optional<CameraFrameSet> completed;
  std::vector<uint32_t> discarded_incomplete_frame_ids;
};

// Reassembles chunked JPEG transfers into one atomic four-camera frame set.
// This keeps protocol bookkeeping out of the window/controller code.
class CameraFrameAssembler {
 public:
  CameraChunkResult ingest(const prism::VideoChunk& chunk);
  CameraChunkResult ingest(const prism::VideoChunkView& chunk);
  std::optional<CameraFrameSet> addMetadata(
      const prism::VideoMeta& metadata);
  void reset();

 private:
  struct ImageBuffer {
    std::vector<uint8_t> jpeg;
    uint32_t encoded_size = 0;
    uint32_t received = 0;
    uint64_t timestamp_us = 0;
  };

  struct PartialFrameSet {
    std::array<std::vector<uint8_t>, 4> jpeg;
    std::array<uint64_t, 4> timestamp_us{};
    uint8_t ready_mask = 0;
  };

  static bool frameIdIsNewer(uint32_t candidate, uint32_t reference);
  bool frameIsSettled(uint32_t frame_id) const;
  bool settleFrame(uint32_t frame_id);
  void discardFrame(uint32_t frame_id, CameraChunkResult* result);
  void discardFramesOlderThan(uint32_t frame_id, CameraChunkResult* result);
  std::optional<CameraFrameSet> takeCompletedFrame(uint32_t frame_id);

  std::map<std::pair<uint8_t, uint32_t>, ImageBuffer> pending_images_;
  std::map<uint32_t, PartialFrameSet> partial_frame_sets_;
  std::map<uint32_t, prism::VideoMeta> metadata_by_frame_;
  std::optional<uint32_t> newest_frame_id_;
  std::set<uint32_t> settled_frame_ids_;
  std::deque<uint32_t> settled_frame_order_;
};

}  // namespace prism_viewer::transfer
