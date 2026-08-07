#include "imu_timestamp_policy.hpp"

#include <cstdlib>
#include <iostream>

namespace {

bool expect(bool actual, bool expected, const char* name) {
  if (actual == expected) return true;
  std::cerr << name << ": expected " << expected << ", got " << actual
            << '\n';
  return false;
}

}  // namespace

int main() {
  using prism_viewer::shouldRebaselineForSyncedFsync;
  using prism_viewer::shouldRebaselineForFirstUtcFsync;
  bool ok = true;
  ok &= expect(shouldRebaselineForSyncedFsync(true, true, true, true, false, 1),
               true, "continuous synchronized FSYNC");
  ok &= expect(shouldRebaselineForSyncedFsync(true, true, true, true, true, 1),
               false, "PL sample gap");
  ok &= expect(shouldRebaselineForSyncedFsync(true, true, true, true, false, 2),
               false, "sequence gap");
  ok &= expect(shouldRebaselineForSyncedFsync(true, true, true, false, false, 1),
               false, "invalid FSYNC delay");
  ok &= expect(shouldRebaselineForSyncedFsync(true, false, true, true, false, 1),
               false, "unsynchronized timestamp");
  ok &= expect(shouldRebaselineForSyncedFsync(false, true, true, true, false, 1),
               false, "uninitialized checker");
  ok &= expect(shouldRebaselineForFirstUtcFsync(
                   true, false, true, true, false, 1,
                   10600000ULL, 1777320041000061ULL),
               true, "first local-to-UTC FSYNC anchor");
  ok &= expect(shouldRebaselineForFirstUtcFsync(
                   true, false, true, true, false, 1,
                   1777320040000061ULL, 1777320041000061ULL),
               false, "later unsynchronized UTC FSYNC");
  ok &= expect(shouldRebaselineForFirstUtcFsync(
                   true, false, true, true, true, 1,
                   10600000ULL, 1777320041000061ULL),
               false, "first UTC FSYNC with PL sample gap");
  ok &= expect(shouldRebaselineForFirstUtcFsync(
                   true, false, true, true, false, 2,
                   10600000ULL, 1777320041000061ULL),
               false, "first UTC FSYNC with sequence gap");
  if (!ok) return EXIT_FAILURE;
  std::cout << "IMU timestamp re-anchor policy passed\n";
  return EXIT_SUCCESS;
}
