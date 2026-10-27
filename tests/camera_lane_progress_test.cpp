#include "ui/camera_lane_progress.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
using Progress = prism_viewer::ui::CameraLaneProgress;
using State = Progress::State;
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
int main() {
  try {
    for (unsigned mask = 0; mask < 16; ++mask) {
      Progress p; p.start(100);
      check(p.snapshot(5099)[0].state == State::Waiting, "premature startup warning");
      p.receive(static_cast<uint8_t>(mask), 1, 5100, true);
      auto lanes = p.snapshot(5100);
      for (unsigned c = 0; c < 4; ++c) {
        check(lanes[c].state == ((mask & (1u << c)) ? State::Live : State::Missing), "wrong missing lane");
        check(lanes[c].count == ((mask & (1u << c)) ? 1u : 0u), "fabricated camera count");
      }
      p.receive(15, 2, 5200, true);
      for (const auto& l : p.snapshot(5200)) check(l.state == State::Live, "rejoin not cleared");
    }
    Progress p; p.start(0); p.receive(15, 1, 0);
    for (unsigned n = 2; n <= 21; ++n) p.receive(13, n, (n-1)*500);
    auto lanes = p.snapshot(10000);
    check(lanes[1].state == State::Missing && lanes[1].fps == 0 && lanes[1].count == 1,
          "other cameras hid independent lane timeout");
    check(lanes[1].minimum_preview_sequence > 1, "stale decode could hide warning");
    check(lanes[0].state == State::Live && std::abs(lanes[0].fps-2) < 0.01,
          "healthy camera rate affected");
    p.receive(2, 22, 10100);
    check(p.snapshot(10100)[1].state == State::Live && p.snapshot(10100)[1].fps == 0,
          "recovery not rebaselined");
    p.receive(13, 23, 10200, true); p.receive(2, 22, 10300);
    check(p.snapshot(10300)[1].state == State::Missing, "stale event hid absence");
    p.reset();
    for (const auto& l : p.snapshot(999999)) check(l.state == State::Waiting && !l.count,
          "reset retained previous session");
    p.start(100); check(p.snapshot(5100)[0].state == State::Missing, "zero-camera timeout missing");
    std::cout << "camera lane progress: masks, independent timeout, rates, recovery, stale and reset PASS\n";
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
