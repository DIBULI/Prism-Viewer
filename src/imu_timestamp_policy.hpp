#pragma once

#include <cstdint>

namespace prism_viewer {

// A synchronized FSYNC sample is a precise PPS re-anchor, so its UTC value
// may legitimately step relative to the sample-by-sample extrapolated clock.
// Only suppress interval checking when transport continuity is independently
// proven by both the sequence number and the PL sample-gap flag.
inline bool shouldRebaselineForSyncedFsync(bool initialized,
                                           bool timestamp_synced,
                                           bool fsync_event,
                                           bool fsync_delay_valid,
                                           bool sample_gap,
                                           uint16_t sequence_delta) {
  return initialized && timestamp_synced && fsync_event &&
         fsync_delay_valid && !sample_gap && sequence_delta == 1u;
}

// The first exact PPS anchor moves the output from sensor-board uptime into
// Unix UTC.  PL deliberately leaves timestamp_synced clear on that one packet
// until a second anchor has calibrated the IMU oscillator scale.  Recognize
// only this one-way local->UTC transition; later unsynchronized UTC-sized
// jumps must still be checked and reported.
inline bool shouldRebaselineForFirstUtcFsync(
    bool initialized, bool timestamp_synced, bool fsync_event,
    bool fsync_delay_valid, bool sample_gap, uint16_t sequence_delta,
    uint64_t previous_timestamp_us, uint64_t current_timestamp_us) {
  constexpr uint64_t kMinPlausibleUnixUtcUs = 946684800000000ULL;
  return initialized && !timestamp_synced && fsync_event &&
         fsync_delay_valid && !sample_gap && sequence_delta == 1u &&
         previous_timestamp_us < kMinPlausibleUnixUtcUs &&
         current_timestamp_us >= kMinPlausibleUnixUtcUs;
}

}  // namespace prism_viewer
