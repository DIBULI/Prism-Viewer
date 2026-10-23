#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>

namespace prism_viewer::ui {

// One latest decoded image per independent camera, and at most one queued UI
// drain per generation. A busy UI may coalesce one lane's old preview, but can
// never let another lane evict it. This is display-only, after transport/record.
template <typename Frame, std::size_t Cameras = 4>
class IndependentPreviewMailbox {
 public:
  struct Result { bool accepted = false, post = false, replaced = false; };
  using Batch = std::array<std::optional<Frame>, Cameras>;

  void reset(uint64_t generation) {
    std::lock_guard<std::mutex> lock(mutex_);
    generation_ = generation;
    latest_.fill(0);
    frames_ = {};
    posted_ = false;
  }

  Result publish(std::size_t camera, uint64_t sequence, uint64_t generation,
                 Frame frame) {
    if (camera >= Cameras) throw std::out_of_range("preview camera");
    std::lock_guard<std::mutex> lock(mutex_);
    if (generation != generation_ || sequence <= latest_[camera]) return {};
    Result result{true, !posted_, frames_[camera].has_value()};
    latest_[camera] = sequence;
    frames_[camera] = std::move(frame);
    posted_ = true;
    return result;
  }

  Batch take(uint64_t generation) {
    std::lock_guard<std::mutex> lock(mutex_);
    // A callback from the previous capture must not drain the new capture or
    // clear its notification flag.
    if (generation != generation_) return {};
    Batch result = std::move(frames_);
    frames_ = {};
    posted_ = false;
    return result;
  }

 private:
  std::mutex mutex_;
  uint64_t generation_ = 0;
  std::array<uint64_t, Cameras> latest_{};
  Batch frames_;
  bool posted_ = false;
};

}  // namespace prism_viewer::ui
