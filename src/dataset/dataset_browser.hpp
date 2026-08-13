#pragma once

#include <QtCore/QString>
#include <QtGui/QImage>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <filesystem>
#include <array>
#include <string>
#include <vector>

namespace prism_viewer::dataset {

struct DatasetImageEntry {
  uint64_t timestamp_us = 0;
  QString absolute_path;
  uint64_t byte_offset = 0;
  uint32_t byte_size = 0;
  uint32_t exposure_us = 0;
};

struct TumFileSummary {
  uint64_t rows = 0;
  uint64_t first_timestamp_us = 0;
  uint64_t last_timestamp_us = 0;
};

enum class DatasetValidationSeverity {
  Warning,
  Error,
};

struct DatasetValidationIssue {
  DatasetValidationSeverity severity = DatasetValidationSeverity::Error;
  std::string file;
  uint64_t line = 0;
  std::string message;
};

struct DatasetTimestampSummary {
  uint64_t rows = 0;
  uint64_t first_timestamp_us = 0;
  uint64_t last_timestamp_us = 0;
  uint64_t median_interval_us = 0;
  uint64_t minimum_interval_us = 0;
  uint64_t maximum_interval_us = 0;
  uint64_t discontinuities = 0;
};

struct DatasetValidationResult {
  bool valid = false;
  bool cancelled = false;
  uint32_t format_version = 0;
  std::string recording_mode;
  bool cameras_present = false;
  bool lidar_present = false;
  bool lidar_imu_present = false;
  uint64_t checked_records = 0;
  std::array<DatasetTimestampSummary, 4> cameras{};
  std::array<DatasetTimestampSummary, 2> onboard_imus{};
  DatasetTimestampSummary lidar;
  DatasetTimestampSummary lidar_imu;
  size_t total_errors = 0;
  size_t total_warnings = 0;
  std::vector<DatasetValidationIssue> issues;

  size_t errorCount() const;
  size_t warningCount() const;
};

using DatasetValidationProgressCallback =
    std::function<void(uint64_t checked_records, const std::string& file)>;
using DatasetValidationCancelCallback = std::function<bool()>;

uint64_t parseTumTimestampUs(const std::string& token);
bool inspectDatasetCameraIndexes(const std::filesystem::path& root,
                                 size_t camera_count,
                                 size_t* present_count,
                                 std::string* error);
bool loadDatasetImageIndex(const std::filesystem::path& root, size_t camera,
                           std::vector<DatasetImageEntry>* entries,
                           std::string* error);
QImage loadDatasetImage(const DatasetImageEntry& entry);
TumFileSummary summarizeTumFile(const std::filesystem::path& path);
DatasetValidationResult validatePrismDataset(
    const std::filesystem::path& root,
    const DatasetValidationProgressCallback& progress = {},
    const DatasetValidationCancelCallback& cancelled = {});

}  // namespace prism_viewer::dataset
