#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <mutex>

namespace prism_viewer::ui {

// Receiver evidence only, independent of lossy preview decoding/rendering.
// No device commands, stream gating or ACK bookkeeping belongs here.
class CameraLaneProgress {
 public:
  enum class State { Waiting, Live, Missing };
  struct Lane {
    State state = State::Waiting;
    bool declared_absent = false;
    uint64_t count = 0, sequence = 0, minimum_preview_sequence = 0;
    int64_t last_ms = 0;
    double fps = 0;
    std::deque<int64_t> samples;
  };
  static constexpr int64_t timeout_ms = 5000;
  void reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    active_ = false; lanes_ = {};
  }
  void start(int64_t now) {
    std::lock_guard<std::mutex> lock(mutex_);
    active_ = true; lanes_ = {};
    for (auto& lane : lanes_) lane.last_ms = now;
  }
  void receive(uint8_t mask, uint64_t sequence, int64_t now,
               bool exact_membership = false) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!active_) return;
    for (unsigned c = 0; c < 4; ++c) {
      auto& lane = lanes_[c];
      if (mask & (1u << c)) {
        if (sequence <= lane.sequence || sequence < lane.minimum_preview_sequence) continue;
        if (lane.state != State::Live || now - lane.last_ms >= timeout_ms)
          lane.samples.clear();
        lane.state = State::Live; lane.declared_absent = false;
        lane.last_ms = now; lane.sequence = sequence; ++lane.count;
        lane.samples.push_back(now);
        while (lane.samples.size() > 256 ||
               (lane.samples.size() > 2 && now - lane.samples.front() > 4000))
          lane.samples.pop_front();
        const auto interval = lane.samples.back() - lane.samples.front();
        lane.fps = interval > 0 ? 1000.0 * (lane.samples.size() - 1) / interval : 0;
      } else if (exact_membership && sequence >= lane.sequence) {
        lane.state = State::Missing; lane.declared_absent = true; lane.fps = 0;
        lane.minimum_preview_sequence = sequence + 1;
      }
    }
  }
  std::array<Lane, 4> snapshot(int64_t now) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (active_) for (auto& lane : lanes_) {
      if (now - lane.last_ms >= timeout_ms && lane.state != State::Missing) {
        lane.state = State::Missing; lane.fps = 0;
        lane.minimum_preview_sequence = lane.sequence + 1;
      }
    }
    return lanes_;
  }
 private:
  std::mutex mutex_;
  bool active_ = false;
  std::array<Lane, 4> lanes_{};
};
}  // namespace prism_viewer::ui
