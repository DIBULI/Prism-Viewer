#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace prism {

struct TimeInfo {
  int64_t unix_ms = 0;
};

struct TimeSyncSample {
  int64_t host_send_unix_us = 0;
  int64_t device_receive_unix_us = 0;
  int64_t device_transmit_unix_us = 0;
  int64_t host_receive_unix_us = 0;
  int64_t round_trip_us = 0;
  int64_t offset_us = 0;  // Device clock minus host clock.
};

struct NtpTimeSyncResult {
  uint32_t requested_samples = 0;
  uint32_t accepted_samples = 0;
  int64_t offset_us = 0;       // Add this to host time to estimate device time.
  int64_t round_trip_us = 0;   // Best accepted round trip.
  int64_t jitter_us = 0;       // Median absolute deviation of accepted offsets.
  int64_t device_time_us = 0;  // Device time estimated at method return.
  std::vector<TimeSyncSample> samples;
};

struct SystemTimeSyncResult {
  NtpTimeSyncResult before;
  NtpTimeSyncResult after;
  int64_t applied_correction_us = 0;
  uint32_t correction_passes = 0;
  bool system_time_set = false;
  bool ptp_hardware_clock_set = false;
  bool hardware_clock_set = false;
  bool verified = false;
  std::string rtc_device;
};

}  // namespace prism
