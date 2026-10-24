#pragma once
#include "dataset/dataset_browser.hpp"

namespace prism_viewer::dataset {
struct Raw12Dataset {
  std::array<std::vector<DatasetImageEntry>, 4> cameras;
  std::array<uint64_t, 4> dropped{};
  std::vector<uint64_t> timestamps;
  // Each slot is the latest actual sample, never a fabricated synchronized frame.
  // A missing/expired slot is -1. All camera timestamps, including extra tail
  // frames, appear in the union timeline.
  std::vector<std::array<int, 4>> display;
  uint64_t bytes = 0;
  bool complete = false;
  // Missing or mixed clock semantics cannot form a common time axis. Retain
  // every image for ordinal/manual browsing instead of inventing timestamps.
  bool ordinal_only = false;
};
Raw12Dataset loadRaw12Dataset(const std::filesystem::path& root,
                             const DatasetValidationCancelCallback& cancelled = {});
QImage loadRaw12DatasetImage(const DatasetImageEntry& entry);
DatasetValidationResult validateRaw12Dataset(
    const std::filesystem::path& root,
    const DatasetValidationProgressCallback& progress,
    const DatasetValidationCancelCallback& cancelled);
// Recorder-only finalization gate. Playback still requires complete=1.
DatasetValidationResult verifyUnpublishedRaw12Dataset(const std::filesystem::path& root);
}  // namespace prism_viewer::dataset
