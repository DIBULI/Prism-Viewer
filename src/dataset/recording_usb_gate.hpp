#pragma once

#include "prism/usb/common.hpp"
#include <chrono>

namespace prism_viewer::dataset {

// GUI-owned status: delayed queued replies must not supersede a newer check.
// Enumeration alone and a USB 3-capable port are not proof of a USB 3 link.
class RecordingUsbGate {
 public:
  using Clock = std::chrono::steady_clock;
  static constexpr auto kMaxAge = std::chrono::seconds(10);

  void observe(prism::UsbLinkSpeed speed, Clock::time_point observed_at) {
    if (observed_at < observed_at_) return;
    speed_ = speed;
    observed_at_ = observed_at;
    observed_ = true;
  }

  void reset() { observe(prism::UsbLinkSpeed::Unknown, Clock::now()); observed_=false; }

  bool allows(bool connected, Clock::time_point now = Clock::now()) const {
    return connected && now >= observed_at_ && now - observed_at_ < kMaxAge &&
        (speed_ == prism::UsbLinkSpeed::UsbSuperSpeed ||
         speed_ == prism::UsbLinkSpeed::UsbSuperSpeedPlus);
  }

  bool mustStop(bool recording, bool connected,
                Clock::time_point now = Clock::now()) const {
    return recording && !allows(connected, now);
  }

  // Network is a separate transport, never pretend it negotiated USB 3.
  // Fresh DeviceInfo is still required; this is not a bandwidth guarantee.
  bool allowsNetwork(bool connected, Clock::time_point now = Clock::now()) const {
    return connected && observed_ && now >= observed_at_ && now-observed_at_ < kMaxAge;
  }

 private:
  prism::UsbLinkSpeed speed_ = prism::UsbLinkSpeed::Unknown;
  Clock::time_point observed_at_{};
  bool observed_=false;
};

}  // namespace prism_viewer::dataset
