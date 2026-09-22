#include "dataset/recording_startup_gate.hpp"
#include <iostream>
#include <stdexcept>

using Gate = prism_viewer::dataset::RecordingStartupGate;
static void check(bool ok) { if (!ok) throw std::runtime_error("startup gate assertion failed"); }
template<class F> static void rejects(F f) {
  bool threw = false;
  try { f(); } catch (const std::runtime_error&) { threw = true; }
  check(threw);
}
static void warm(Gate& gate, unsigned id, uint64_t base = 1000000,
                 uint32_t seq = 0, uint64_t elapsed = 0) {
  for (unsigned i = 0; i <= 200; ++i)
    check(!gate.imu(id, base + i * 1250, (seq+i)&0xffff,
                    true, false, elapsed+i*1250));
}
int main() {
  try {
    Gate gate(1);
    check(!gate.admit(Gate::Camera, 9387000637));
    check(!gate.imu(0, 9387000637, 0, false, false, 0));
    check(!gate.imu(0, 9387000625, 1, false, false, 1250));
    check(!gate.ready() && gate.backsteps[0] == 1);
    warm(gate, 0, 9387001875, 2, 2500);
    check(gate.ready() && gate.start_us == 9387251875);
    check(!gate.admit(Gate::Camera, gate.start_us));
    check(gate.admit(Gate::Camera, gate.start_us + 1));
    rejects([&] { gate.admit(Gate::Camera, gate.start_us + 1); });
    check(gate.imu(0, gate.start_us + 1250, 203, true, false, 253750));
    rejects([&] { gate.imu(0, gate.start_us + 1238, 204, true, false, 255000); });
    rejects([&] { gate.imu(0, gate.start_us + 2500, 205, false, false, 256250); });
    rejects([&] { gate.imu(0, gate.start_us + 3750, 206, true, true, 257500); });
    check(gate.ready());

    Gate dual(3);
    warm(dual, 0);
    check(!dual.ready());
    warm(dual, 1, 1000100);
    check(dual.ready() && dual.start_us == 1250100);
    check(!dual.imu(0, 1250000, 201, true, false, 251250));
    check(dual.imu(0, 1251250, 202, true, false, 252500));
    check(dual.admit(Gate::Lidar, 1251251));
    rejects([&] { dual.admit(Gate::Lidar, 1251250); });

    Gate imu1only(2);
    check(!imu1only.imu(0, 1, 0, true, false, 0));
    warm(imu1only, 1, 1000000, 65500);
    check(imu1only.ready());

    Gate reset(1);
    for (unsigned i=0;i<199;++i)
      reset.imu(0, 1000000+i*1250, i, true, false, i*1250);
    reset.imu(0, 1248750, 199, true, true, 248750);
    check(!reset.ready());
    warm(reset, 0, 1250000, 200, 250000);
    check(reset.ready() && reset.resets[0] == 1);

    Gate stale(3);
    warm(stale, 0);
    warm(stale, 1, 2000000, 0, 1000000);
    check(!stale.ready());
    rejects([&] { stale.checkTimeout(Gate::kTimeoutUs); });
    Gate silent(1);
    silent.checkTimeout(Gate::kTimeoutUs-1);
    rejects([&] { silent.checkTimeout(Gate::kTimeoutUs); });
    std::cout << "recording startup gate: PASS\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n'; return 1;
  }
}
