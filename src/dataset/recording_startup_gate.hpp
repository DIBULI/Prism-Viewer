#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>

namespace prism_viewer::dataset {

// Internal IMU/FSYNC synchronization only: no GPS or UTC prerequisite.
// Sensor timestamps are never repaired, clamped or replaced with host time.
class RecordingStartupGate {
 public:
  enum Stream : unsigned { Imu0, Imu1, Camera, Lidar, LidarImu };
  static constexpr uint64_t kStableUs = 250000;
  static constexpr uint64_t kTimeoutUs = 15000000;

  explicit RecordingStartupGate(unsigned required_mask = 3)
      : required_mask_(required_mask) {
    if (!required_mask || (required_mask & ~3u))
      throw std::invalid_argument("invalid recording IMU mask");
  }

  bool imu(unsigned id, uint64_t timestamp, uint32_t sequence,
           bool synced, bool gap, uint64_t elapsed_us) {
    if (id >= 2 || !(required_mask_ & (1u << id))) return false;
    if (ready_) {
      if (!synced || gap)
        throw std::runtime_error("IMU synchronization lost or sample gap during recording; start a new dataset");
      return admit(static_cast<Stream>(id), timestamp);
    }
    checkTimeout(elapsed_us);
    auto& s = sensors_[id];
    if (s.previous && timestamp <= s.previous) ++backsteps[id];
    const auto delta = static_cast<uint16_t>(sequence - s.sequence);
    const bool continuous = synced && !gap && timestamp && s.synced &&
        delta == 1 && timestamp > s.previous &&
        timestamp - s.previous >= 625 && timestamp - s.previous <= 1875 &&
        elapsed_us >= s.received && elapsed_us - s.received <= 500000;
    if (!continuous) {
      if (s.count) ++resets[id];
      s.count = 0;
      s.first = timestamp;
    }
    if (synced && !gap && timestamp) ++s.count;
    s.previous = timestamp;
    s.sequence = sequence;
    s.received = elapsed_us;
    s.synced = synced && !gap;
    if (readyMask(elapsed_us) == required_mask_) {
      ready_ = true;
      ready_elapsed_us = elapsed_us;
      for (unsigned i = 0; i < 2; ++i)
        if (required_mask_ & (1u << i)) start_us = std::max(start_us, sensors_[i].previous);
    }
    return false;  // The boundary sample belongs to startup diagnostics.
  }

  unsigned readyMask(uint64_t elapsed_us) const {
    if (ready_) return required_mask_;
    unsigned mask = 0;
    for (unsigned i = 0; i < 2; ++i) {
      const auto& s = sensors_[i];
      if ((required_mask_ & (1u << i)) && s.count >= 200 &&
          s.previous >= s.first && s.previous - s.first >= kStableUs &&
          elapsed_us >= s.received && elapsed_us - s.received <= 500000)
        mask |= 1u << i;
    }
    return mask;
  }

  bool admit(Stream stream, uint64_t timestamp) {
    if (!ready_) return false;
    auto& last = last_[stream];
    if (!last && timestamp <= start_us) return false;
    if (!timestamp || (last && timestamp <= last))
      throw std::runtime_error("sensor timestamp moved backwards or repeated during recording; start a new dataset");
    last = timestamp;
    return true;
  }

  void checkTimeout(uint64_t elapsed_us) const {
    if (!ready_ && elapsed_us >= kTimeoutUs)
      throw std::runtime_error("IMU startup synchronization timed out after 15 s; GPS is not required");
  }
  bool ready() const { return ready_; }
  unsigned requiredMask() const { return required_mask_; }
  uint64_t start_us = 0, ready_elapsed_us = 0;
  std::array<uint64_t, 2> backsteps{}, resets{};

 private:
  struct Sensor {
    uint64_t previous = 0, first = 0, received = 0;
    uint32_t sequence = 0, count = 0;
    bool synced = false;
  };
  unsigned required_mask_;
  bool ready_ = false;
  std::array<Sensor, 2> sensors_{};
  std::array<uint64_t, 5> last_{};
};

}  // namespace prism_viewer::dataset
