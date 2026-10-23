#include "dataset/recording_usb_gate.hpp"
#include <iostream>
#include <stdexcept>

using Gate = prism_viewer::dataset::RecordingUsbGate;
using Speed = prism::UsbLinkSpeed;
static void check(bool value) {
  if (!value) throw std::runtime_error("USB recording gate assertion failed");
}

int main() {
  try {
    Gate gate;
    const auto now = Gate::Clock::now();
    check(!gate.allows(true, now));
    check(!gate.allowsNetwork(true, now));
    for (unsigned i = 0; i < 256; ++i) {
      const auto speed = static_cast<Speed>(i);
      gate.observe(speed, now);
      const bool usb3 = speed == Speed::UsbSuperSpeed || speed == Speed::UsbSuperSpeedPlus;
      check(gate.allows(true, now) == usb3);
      check(!gate.allows(false, now));
      check(gate.mustStop(true, true, now) == !usb3);
      check(!gate.mustStop(false, true, now));
    }
    gate.observe(Speed::UsbSuperSpeed, now);
    check(gate.allows(true, now + Gate::kMaxAge - std::chrono::milliseconds(1)));
    check(!gate.allows(true, now + Gate::kMaxAge));
    check(!gate.allows(true, now - std::chrono::milliseconds(1)));
    const auto later = now + std::chrono::seconds(1);
    gate.observe(Speed::UsbHighSpeed, later);
    gate.observe(Speed::UsbSuperSpeed, now); // Delayed response, not recovery.
    check(gate.mustStop(true, true, later));
    gate.observe(Speed::UsbSuperSpeedPlus, later + std::chrono::seconds(1));
    check(gate.allows(true, later + std::chrono::seconds(1)));
    gate.observe(Speed::Unknown, later + std::chrono::seconds(2));
    check(gate.allowsNetwork(true,later+std::chrono::seconds(2)));
    check(!gate.allowsNetwork(false,later+std::chrono::seconds(2)));
    check(!gate.allowsNetwork(true,later+std::chrono::seconds(2)+Gate::kMaxAge));
    check(gate.mustStop(true, true, later + std::chrono::seconds(2)));
    Gate reopened;
    reopened.observe(Speed::UsbSuperSpeed, Gate::Clock::now());
    reopened.reset();
    check(!reopened.allows(true));
    check(!reopened.allowsNetwork(true));
    std::cout << "USB recording gate: PASS\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
