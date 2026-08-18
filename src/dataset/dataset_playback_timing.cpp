#include "dataset/dataset_playback_timing.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace prism_viewer::dataset {

double sanitizeDatasetPlaybackRate(double rate) {
  return std::isfinite(rate) && rate > 0.0 ? rate : 1.0;
}

int datasetPlaybackDelayMs(uint64_t current_timestamp_us,
                           uint64_t next_timestamp_us, double rate) {
  if (next_timestamp_us <= current_timestamp_us) {
    return kDatasetPlaybackMinimumDelayMs;
  }

  const long double delay_ms =
      static_cast<long double>(next_timestamp_us - current_timestamp_us) /
      (static_cast<long double>(sanitizeDatasetPlaybackRate(rate)) * 1000.0L);
  if (delay_ms <=
      static_cast<long double>(kDatasetPlaybackMinimumDelayMs)) {
    return kDatasetPlaybackMinimumDelayMs;
  }
  if (delay_ms >=
      static_cast<long double>(kDatasetPlaybackMaximumDelayMs)) {
    return kDatasetPlaybackMaximumDelayMs;
  }
  return static_cast<int>(std::llround(delay_ms));
}

}  // namespace prism_viewer::dataset
