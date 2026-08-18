#include "dataset/dataset_playback_timing.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

}  // namespace

int main() {
  using prism_viewer::dataset::datasetPlaybackDelayMs;
  using prism_viewer::dataset::sanitizeDatasetPlaybackRate;

  require(datasetPlaybackDelayMs(1000000, 1033350, 1.0) == 33,
          "1x playback delay does not follow timestamps");
  require(datasetPlaybackDelayMs(1000000, 1033350, 0.5) == 67,
          "0.5x playback delay is wrong");
  require(datasetPlaybackDelayMs(1000000, 1033350, 2.0) == 17,
          "2x playback delay is wrong");
  require(datasetPlaybackDelayMs(1000000, 1033350, 8.0) == 4,
          "8x playback delay is wrong");
  require(datasetPlaybackDelayMs(1000000, 1000000, 1.0) == 1,
          "duplicate timestamps must remain responsive");
  require(datasetPlaybackDelayMs(1000000, 999999, 1.0) == 1,
          "backward timestamps must remain responsive");
  require(datasetPlaybackDelayMs(0, 120000000, 1.0) == 60000,
          "large timestamp gaps must be bounded");
  require(sanitizeDatasetPlaybackRate(0.0) == 1.0,
          "zero playback rate was not sanitized");
  require(sanitizeDatasetPlaybackRate(-2.0) == 1.0,
          "negative playback rate was not sanitized");
  require(sanitizeDatasetPlaybackRate(
              std::numeric_limits<double>::quiet_NaN()) == 1.0,
          "NaN playback rate was not sanitized");

  std::cout << "Dataset playback timing tests passed\n";
  return EXIT_SUCCESS;
}
