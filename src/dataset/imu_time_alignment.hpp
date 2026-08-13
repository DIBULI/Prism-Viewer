#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace prism_viewer::dataset {

enum class ImuTimeAlignmentStatus {
  Success,
  MissingInput,
  InvalidData,
  InsufficientOverlap,
  InsufficientMotion,
  LowConfidence,
  Cancelled,
};

struct ImuTimeAlignmentResult {
  bool valid = false;
  bool cancelled = false;
  ImuTimeAlignmentStatus status = ImuTimeAlignmentStatus::InvalidData;
  std::string message;

  uint64_t imu0_rows = 0;
  uint64_t lidar_imu_rows = 0;
  uint64_t analyzed_imu0_samples = 0;
  uint64_t analyzed_lidar_imu_samples = 0;
  uint64_t common_start_us = 0;
  uint64_t common_end_us = 0;
  double analyzed_duration_s = 0.0;

  // For the same physical motion:
  //   offset = timestamp_lidar_imu - timestamp_imu0
  // Subtract this value from LiDAR IMU timestamps to align them to IMU0.
  double offset_us = 0.0;
  double correlation = 0.0;
  double correlation_peak_width_us = 0.0;
  double motion_stddev_dps = 0.0;
  double motion_peak_to_peak_dps = 0.0;
};

using ImuTimeAlignmentProgressCallback =
    std::function<void(uint64_t checked_rows, const std::string& file)>;
using ImuTimeAlignmentCancelCallback = std::function<bool()>;

ImuTimeAlignmentResult analyzeImu0LidarImuTimeOffset(
    const std::filesystem::path& dataset_root,
    const ImuTimeAlignmentProgressCallback& progress = {},
    const ImuTimeAlignmentCancelCallback& cancelled = {});

}  // namespace prism_viewer::dataset
