#include "transfer/camera_frame_assembler.hpp"

#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <vector>

namespace {

prism::VideoChunk makeChunk(uint8_t camera, uint32_t frame,
                            uint32_t offset,
                            std::initializer_list<uint8_t> bytes) {
  prism::VideoChunk chunk;
  chunk.camera_id = camera;
  chunk.frame_id = frame;
  chunk.encoded_size = 4;
  chunk.chunk_offset = offset;
  chunk.chunk_size = static_cast<uint32_t>(bytes.size());
  chunk.timestamp_us = 1000 + camera;
  chunk.data.assign(bytes.begin(), bytes.end());
  return chunk;
}

bool isSingleDiscard(
    const prism_viewer::transfer::CameraChunkResult& result,
    uint32_t frame_id) {
  return !result.completed.has_value() &&
         result.discarded_incomplete_frame_ids.size() == 1 &&
         result.discarded_incomplete_frame_ids.front() == frame_id;
}

}  // namespace

int main() {
  prism_viewer::transfer::CameraFrameAssembler assembler;
  prism::VideoMeta metadata;
  metadata.valid = true;
  metadata.host_frame_id = 42;
  metadata.trigger_time_ns = 9876543000ULL;
  metadata.exposure_us = {200u, 202u, 303u, 404u};
  assembler.addMetadata(metadata);

  for (uint8_t camera = 0; camera < 4; ++camera) {
    auto first = assembler.ingest(
        makeChunk(camera, 42, 0, {camera, static_cast<uint8_t>(camera + 1)}));
    if (first.completed.has_value()) {
      std::cerr << "frame completed before all cameras arrived\n";
      return 1;
    }
    auto second = assembler.ingest(
        makeChunk(camera, 42, 2,
                  {static_cast<uint8_t>(camera + 2),
                   static_cast<uint8_t>(camera + 3)}));
    if (camera < 3 && second.completed.has_value()) {
      std::cerr << "frame completed before camera 3 arrived\n";
      return 2;
    }
    if (camera == 3) {
      if (!second.completed.has_value()) {
        std::cerr << "complete four-camera frame was not emitted\n";
        return 3;
      }
      const auto& frame = *second.completed;
      if (frame.frame_id != 42 || frame.timestamp_us != 9876543ULL) {
        std::cerr << "frame identity or metadata timestamp changed\n";
        return 4;
      }
      if (frame.metadata.exposure_us != metadata.exposure_us) {
        std::cerr << "frame metadata exposure was not preserved\n";
        return 5;
      }
      for (uint8_t id = 0; id < 4; ++id) {
        const std::vector<uint8_t> expected = {
            id, static_cast<uint8_t>(id + 1),
            static_cast<uint8_t>(id + 2), static_cast<uint8_t>(id + 3)};
        if (frame.jpeg[id] != expected) {
          std::cerr << "camera payload was assembled incorrectly\n";
          return 6;
        }
      }
    }
  }

  /*
   * The server window is four frame sets. Advancing to a newer frame must
   * immediately retire an incomplete predecessor instead of retaining more
   * than the server can send without an ACK.
   */
  prism_viewer::transfer::CameraFrameAssembler window_assembler;
  for (uint32_t frame = 100; frame < 104; ++frame) {
    auto partial = window_assembler.ingest(makeChunk(0, frame, 0, {1, 2}));
    if (frame == 100) {
      if (!partial.discarded_incomplete_frame_ids.empty()) {
        std::cerr << "first partial frame was discarded immediately\n";
        return 7;
      }
    } else if (!isSingleDiscard(partial, frame - 1)) {
      std::cerr << "new frame did not retire exactly one incomplete predecessor\n";
      return 8;
    }
  }

  // Chunks from an already retired frame cannot release credit twice.
  auto late = window_assembler.ingest(makeChunk(0, 102, 2, {3, 4}));
  if (late.completed.has_value() ||
      !late.discarded_incomplete_frame_ids.empty()) {
    std::cerr << "late chunk settled an old frame more than once\n";
    return 9;
  }

  // A hole in an ordered image is corruption and retires its frame once.
  prism_viewer::transfer::CameraFrameAssembler corrupt_assembler;
  auto corrupt_first =
      corrupt_assembler.ingest(makeChunk(0, 200, 0, {1, 2}));
  if (!corrupt_first.discarded_incomplete_frame_ids.empty()) {
    std::cerr << "valid first chunk was rejected\n";
    return 10;
  }
  auto corrupt_gap =
      corrupt_assembler.ingest(makeChunk(0, 200, 3, {3}));
  if (!isSingleDiscard(corrupt_gap, 200)) {
    std::cerr << "non-contiguous chunk did not retire corrupt frame\n";
    return 11;
  }
  auto corrupt_duplicate =
      corrupt_assembler.ingest(makeChunk(0, 200, 3, {3}));
  if (!corrupt_duplicate.discarded_incomplete_frame_ids.empty()) {
    std::cerr << "corrupt frame was retired more than once\n";
    return 12;
  }

  // Invalid camera IDs also consume and retire exactly one transmitted set.
  prism_viewer::transfer::CameraFrameAssembler invalid_camera_assembler;
  auto invalid_camera =
      invalid_camera_assembler.ingest(makeChunk(4, 300, 0, {1, 2}));
  if (!isSingleDiscard(invalid_camera, 300)) {
    std::cerr << "invalid camera ID did not retire frame\n";
    return 13;
  }
  auto invalid_camera_again =
      invalid_camera_assembler.ingest(makeChunk(4, 300, 0, {1, 2}));
  if (!invalid_camera_again.discarded_incomplete_frame_ids.empty()) {
    std::cerr << "invalid camera frame was retired more than once\n";
    return 14;
  }

  // Metadata can follow all four JPEGs in the shared USB stream. Keep the
  // completed images pending until their exact per-frame exposure arrives.
  prism_viewer::transfer::CameraFrameAssembler late_metadata_assembler;
  for (uint8_t camera = 0; camera < 4; ++camera) {
    (void)late_metadata_assembler.ingest(
        makeChunk(camera, 400, 0, {camera, camera}));
    const auto second = late_metadata_assembler.ingest(
        makeChunk(camera, 400, 2, {camera, camera}));
    if (second.completed.has_value()) {
      std::cerr << "frame completed before late metadata arrived\n";
      return 15;
    }
  }
  prism::VideoMeta late_metadata;
  late_metadata.valid = true;
  late_metadata.host_frame_id = 400;
  late_metadata.trigger_time_ns = 1234567000ULL;
  late_metadata.exposure_us = {200u, 250u, 350u, 450u};
  const auto late_completed =
      late_metadata_assembler.addMetadata(late_metadata);
  if (!late_completed.has_value() ||
      late_completed->metadata.exposure_us != late_metadata.exposure_us ||
      late_completed->timestamp_us != 1234567ULL) {
    std::cerr << "late metadata did not complete the frame with exposure\n";
    return 16;
  }

  // A valid metadata row can still carry trigger_time_ns=0 while the
  // sensor-board is waiting for its RK time anchor. The per-JPEG timestamp is
  // an Agent monotonic/encode timestamp and must never masquerade as an RK
  // CLOCK_REALTIME measurement timestamp.
  prism_viewer::transfer::CameraFrameAssembler unsynced_assembler;
  prism::VideoMeta unsynced_metadata;
  unsynced_metadata.valid = true;
  unsynced_metadata.host_frame_id = 500;
  unsynced_metadata.trigger_time_ns = 0;
  unsynced_metadata.exposure_us = {200u, 250u, 350u, 450u};
  unsynced_assembler.addMetadata(unsynced_metadata);
  std::optional<prism_viewer::transfer::CameraFrameSet> unsynced_completed;
  for (uint8_t camera = 0; camera < 4; ++camera) {
    (void)unsynced_assembler.ingest(
        makeChunk(camera, 500, 0, {camera, camera}));
    const auto second = unsynced_assembler.ingest(
        makeChunk(camera, 500, 2, {camera, camera}));
    if (second.completed.has_value()) unsynced_completed = second.completed;
  }
  if (!unsynced_completed.has_value() ||
      unsynced_completed->timestamp_us != 0) {
    std::cerr << "unsynchronized camera frame used a monotonic fallback\n";
    return 17;
  }

  return 0;
}
