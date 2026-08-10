#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace prism_viewer::dataset {

enum class RosbagFormat {
  Ros1,
  Ros2,
};

struct RosbagExportProgress {
  uint64_t completed_records = 0;
  uint64_t total_records = 0;
  std::string stage;
};

struct RosbagExportResult {
  bool success = false;
  bool cancelled = false;
  uint64_t camera_messages = 0;
  // Board-mounted IMU0/IMU1 messages. Kept separate from the optional
  // LiDAR-integrated IMU for backwards-compatible result accounting.
  uint64_t imu_messages = 0;
  uint64_t lidar_imu_messages = 0;
  uint64_t lidar_messages = 0;
  uint64_t lidar_points = 0;
  uint64_t output_bytes = 0;
  std::string error;
};

using RosbagProgressCallback =
    std::function<void(const RosbagExportProgress&)>;
using RosbagCancelCallback = std::function<bool()>;

RosbagExportResult exportDatasetToRosbag(
    const std::filesystem::path& dataset_root,
    const std::filesystem::path& output_path, RosbagFormat format,
    bool overwrite,
    const RosbagProgressCallback& progress = {},
    const RosbagCancelCallback& cancelled = {});

}  // namespace prism_viewer::dataset
