#pragma once

#include <cstdint>

namespace prism_viewer::dataset {

constexpr int kDatasetPlaybackMinimumDelayMs = 1;
constexpr int kDatasetPlaybackMaximumDelayMs = 60000;

double sanitizeDatasetPlaybackRate(double rate);
int datasetPlaybackDelayMs(uint64_t current_timestamp_us,
                           uint64_t next_timestamp_us, double rate);

}  // namespace prism_viewer::dataset
