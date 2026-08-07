#include "transfer/camera_frame_assembler.hpp"

#include <algorithm>
#include <limits>
#include <set>

namespace prism_viewer::transfer {
namespace {

constexpr size_t kCameraCount = 4;
constexpr size_t kSettledFrameHistory = 64;

}  // namespace

bool CameraFrameAssembler::frameIdIsNewer(uint32_t candidate,
                                          uint32_t reference) {
  return static_cast<int32_t>(candidate - reference) > 0;
}

bool CameraFrameAssembler::frameIsSettled(uint32_t frame_id) const {
  return settled_frame_ids_.find(frame_id) != settled_frame_ids_.end();
}

bool CameraFrameAssembler::settleFrame(uint32_t frame_id) {
  if (!settled_frame_ids_.insert(frame_id).second) {
    return false;
  }
  settled_frame_order_.push_back(frame_id);
  while (settled_frame_order_.size() > kSettledFrameHistory) {
    settled_frame_ids_.erase(settled_frame_order_.front());
    settled_frame_order_.pop_front();
  }
  return true;
}

void CameraFrameAssembler::discardFrame(uint32_t frame_id,
                                        CameraChunkResult* result) {
  for (auto pending = pending_images_.begin();
       pending != pending_images_.end();) {
    if (pending->first.second == frame_id) {
      pending = pending_images_.erase(pending);
    } else {
      ++pending;
    }
  }
  partial_frame_sets_.erase(frame_id);
  metadata_by_frame_.erase(frame_id);
  if (settleFrame(frame_id)) {
    result->discarded_incomplete_frame_ids.push_back(frame_id);
  }
}

void CameraFrameAssembler::discardFramesOlderThan(
    uint32_t frame_id, CameraChunkResult* result) {
  std::set<uint32_t> incomplete;
  for (const auto& pending : pending_images_) {
    if (frameIdIsNewer(frame_id, pending.first.second)) {
      incomplete.insert(pending.first.second);
    }
  }
  for (const auto& partial : partial_frame_sets_) {
    if (frameIdIsNewer(frame_id, partial.first)) {
      incomplete.insert(partial.first);
    }
  }
  for (uint32_t incomplete_frame_id : incomplete) {
    discardFrame(incomplete_frame_id, result);
  }
}

std::optional<CameraFrameSet> CameraFrameAssembler::takeCompletedFrame(
    uint32_t frame_id) {
  const auto frame_set = partial_frame_sets_.find(frame_id);
  const auto metadata = metadata_by_frame_.find(frame_id);
  if (frame_set == partial_frame_sets_.end() ||
      frame_set->second.ready_mask != 0x0fu ||
      metadata == metadata_by_frame_.end()) {
    return std::nullopt;
  }

  CameraFrameSet completed;
  completed.frame_id = frame_id;
  completed.metadata = metadata->second;
  if (completed.metadata.valid &&
      completed.metadata.trigger_time_ns != 0) {
    completed.timestamp_us =
        completed.metadata.trigger_time_ns / 1000ULL;
  } else {
    for (uint64_t camera_timestamp : frame_set->second.timestamp_us) {
      if (camera_timestamp != 0 &&
          (completed.timestamp_us == 0 ||
           camera_timestamp < completed.timestamp_us)) {
        completed.timestamp_us = camera_timestamp;
      }
    }
  }
  completed.jpeg = std::move(frame_set->second.jpeg);
  partial_frame_sets_.erase(frame_set);
  metadata_by_frame_.erase(metadata);
  settleFrame(frame_id);
  return completed;
}

CameraChunkResult CameraFrameAssembler::ingest(
    const prism::VideoChunk& chunk) {
  prism::VideoChunkView view;
  view.camera_id = chunk.camera_id;
  view.format = chunk.format;
  view.flags = chunk.flags;
  view.width = chunk.width;
  view.height = chunk.height;
  view.frame_id = chunk.frame_id;
  view.encoded_size = chunk.encoded_size;
  view.chunk_offset = chunk.chunk_offset;
  view.chunk_size = chunk.chunk_size;
  view.timestamp_us = chunk.timestamp_us;
  view.data = chunk.data.data();
  view.data_size = chunk.data.size();
  return ingest(view);
}

CameraChunkResult CameraFrameAssembler::ingest(
    const prism::VideoChunkView& chunk) {
  CameraChunkResult result;
  if (newest_frame_id_.has_value()) {
    if (frameIdIsNewer(chunk.frame_id, *newest_frame_id_)) {
      /*
       * The agent serializes a complete four-camera frame set before it
       * starts the next frame ID. Once a newer ID is visible, any older
       * partial image can never receive another chunk. Retire it immediately
       * so the caller can return its one unit of flow-control credit; keeping
       * more partial sets than the four-frame server window deadlocks both
       * sides.
       */
      discardFramesOlderThan(chunk.frame_id, &result);
      newest_frame_id_ = chunk.frame_id;
    } else if (chunk.frame_id != *newest_frame_id_) {
      // USB bulk delivery is ordered. A late chunk belongs to a frame that
      // has already been completed or explicitly discarded.
      return result;
    }
  } else {
    newest_frame_id_ = chunk.frame_id;
  }

  if (frameIsSettled(chunk.frame_id)) {
    return result;
  }

  const size_t camera = static_cast<size_t>(chunk.camera_id);
  const bool size_overflows =
      chunk.chunk_offset >
      std::numeric_limits<uint32_t>::max() - chunk.chunk_size;
  const bool invalid_chunk =
      camera >= kCameraCount || chunk.encoded_size == 0 ||
      chunk.chunk_size == 0 || chunk.data == nullptr ||
      chunk.data_size != chunk.chunk_size ||
      size_overflows ||
      chunk.chunk_offset + chunk.chunk_size > chunk.encoded_size;
  if (invalid_chunk) {
    discardFrame(chunk.frame_id, &result);
    return result;
  }

  const auto key = std::make_pair(chunk.camera_id, chunk.frame_id);
  auto& image = pending_images_[key];
  if (image.received == 0) {
    image.encoded_size = chunk.encoded_size;
    image.jpeg.reserve(chunk.encoded_size);
    image.timestamp_us = chunk.timestamp_us;
  }

  /*
   * Chunks travel over one ordered USB bulk stream. Requiring the next
   * contiguous offset detects duplicates, holes and encoded-size changes;
   * byte-count accumulation alone could otherwise declare a JPEG complete
   * while part of it was never received.
   */
  if (image.encoded_size != chunk.encoded_size ||
      image.timestamp_us != chunk.timestamp_us ||
      chunk.chunk_offset != image.received) {
    discardFrame(chunk.frame_id, &result);
    return result;
  }
  image.jpeg.insert(image.jpeg.end(), chunk.data,
                    chunk.data + chunk.data_size);
  image.received += chunk.chunk_size;
  if (image.received < chunk.encoded_size) {
    return result;
  }

  auto& frame_set = partial_frame_sets_[chunk.frame_id];
  if ((frame_set.ready_mask & (1u << camera)) != 0) {
    discardFrame(chunk.frame_id, &result);
    return result;
  }
  if (image.jpeg.size() != image.encoded_size) {
    discardFrame(chunk.frame_id, &result);
    return result;
  }
  frame_set.jpeg[camera] = std::move(image.jpeg);
  frame_set.timestamp_us[camera] = image.timestamp_us;
  frame_set.ready_mask =
      static_cast<uint8_t>(frame_set.ready_mask | (1u << camera));
  if (frame_set.ready_mask == 0x0fu) {
    result.completed = takeCompletedFrame(chunk.frame_id);
  }
  pending_images_.erase(key);
  return result;
}

std::optional<CameraFrameSet> CameraFrameAssembler::addMetadata(
    const prism::VideoMeta& metadata) {
  if (frameIsSettled(metadata.host_frame_id)) {
    return std::nullopt;
  }
  metadata_by_frame_[metadata.host_frame_id] = metadata;
  std::optional<CameraFrameSet> completed =
      takeCompletedFrame(metadata.host_frame_id);
  while (metadata_by_frame_.size() > 16) {
    metadata_by_frame_.erase(metadata_by_frame_.begin());
  }
  return completed;
}

void CameraFrameAssembler::reset() {
  pending_images_.clear();
  partial_frame_sets_.clear();
  metadata_by_frame_.clear();
  newest_frame_id_.reset();
  settled_frame_ids_.clear();
  settled_frame_order_.clear();
}

}  // namespace prism_viewer::transfer
