#pragma once

#include <chrono>
#include <cstdint>
#include <deque>

namespace prism_viewer::common {

// Sliding-window sample rate estimate. Anchors are recorded at a fixed period
// and retired once they fall outside the window, so the reported rate reflects
// recent throughput rather than the whole session average.
class SampleRateTracker {
 public:
  explicit SampleRateTracker(
      std::chrono::steady_clock::duration window = std::chrono::seconds(5))
      : window_(window) {}

  void add(std::chrono::steady_clock::time_point now) {
    ++total_samples_;
    if (anchors_.empty()) {
      anchors_.push_back({now, total_samples_});
      next_anchor_ = now + kAnchorPeriod;
      return;
    }
    if (now < next_anchor_) return;

    anchors_.push_back({now, total_samples_});
    next_anchor_ = now + kAnchorPeriod;
    while (anchors_.size() > 2 && now - anchors_[1].time >= window_) {
      anchors_.pop_front();
    }
  }

  double rate(std::chrono::steady_clock::time_point now) const {
    if (anchors_.empty()) return 0.0;
    const double elapsed =
        std::chrono::duration<double>(now - anchors_.front().time).count();
    if (elapsed <= 0.0) return 0.0;
    return static_cast<double>(total_samples_ - anchors_.front().sample_count) /
           elapsed;
  }

 private:
  struct Anchor {
    std::chrono::steady_clock::time_point time;
    uint64_t sample_count = 0;
  };

  static constexpr auto kAnchorPeriod = std::chrono::milliseconds(100);

  std::chrono::steady_clock::duration window_;
  std::deque<Anchor> anchors_;
  std::chrono::steady_clock::time_point next_anchor_{};
  uint64_t total_samples_ = 0;
};

}  // namespace prism_viewer::common
