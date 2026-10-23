#pragma once

#include "prism/usb/exposure.hpp"
#include <chrono>
#include <stdexcept>

namespace prism_viewer {
// Owned only by the capture worker. No read, STOP, retry or frame filtering.
class LiveExposureRows {
 public:
  using Clock = std::chrono::steady_clock;
  template<class Client>
  void begin(Client& client, const prism::ExposureConfiguration& config,
             Clock::time_point now = Clock::now()) {
    if (waiting_ || client.liveExposureRowsPending())
      throw std::runtime_error("Previous exposure result is unconfirmed; do not retry");
    if (config.automatic_camera_mask || config.unified_automatic)
      throw std::invalid_argument("Live 415M exposure requires manual rows");
    std::array<uint16_t, 4> rows{};
    for (size_t i = 0; i < rows.size(); ++i) {
      const uint64_t n = uint64_t(config.manual_exposure_time_us[i]) * 2 / 125;
      if (n < 3 || n > 4096 || (n * 125 + 1) / 2 != config.manual_exposure_time_us[i] ||
          config.gain_x1024[i] != 1024)
        throw std::invalid_argument("Live 415M exposure requires integer rows 3..4096 and 1x gain");
      rows[i] = static_cast<uint16_t>(n);
    }
    client.beginLiveExposureRows(0x0f, rows);
    waiting_ = true;
    warned_ = false;
    deadline_ = now + std::chrono::seconds(5);
  }
  template<class Client>
  bool accept(Client& client, const prism::Frame& frame,
              prism::ExposureConfiguration& applied) {
    if (!waiting_) return false;
    try {
      if (!client.finishLiveExposureRows(frame, applied)) return false;
      waiting_ = false;
      return true;
    } catch (...) {
      waiting_ = false; // SDK retains a fail-closed pending state on invalid replies.
      throw;
    }
  }
  bool expired(Clock::time_point now = Clock::now()) {
    if (!waiting_ || warned_ || now < deadline_) return false;
    warned_ = true;
    return true; // warn once, continue receiving all frames and late responses
  }
  bool waiting() const { return waiting_; }
  void reset() { waiting_ = false; warned_ = false; }
 private:
  bool waiting_ = false;
  bool warned_ = false;
  Clock::time_point deadline_{};
};
} // namespace prism_viewer
