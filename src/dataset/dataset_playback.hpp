#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace prism_viewer::dataset {

enum class DatasetPlaybackEventType {
  CameraFrame,
  OnboardImu0,
  OnboardImu1,
  LidarPoints,
  LidarImu,
};

struct DatasetPlaybackImuSample {
  uint64_t timestamp_us = 0;
  uint8_t sensor_id = 0;
  std::array<double, 3> acceleration_m_s2{};
  std::array<double, 3> angular_velocity_rad_s{};
};

struct DatasetPlaybackLidarBatch {
  uint64_t timestamp_us = 0;
  std::filesystem::path container_path;
  uint64_t byte_offset = 0;
  uint32_t byte_size = 0;
  uint32_t point_count = 0;
  uint8_t model = 0;
  uint8_t device_type = 0;
  uint8_t time_type = 0;
  uint32_t batch_id = 0;
  uint64_t timestamp_raw = 0;
  uint16_t time_interval_100ns = 0;
  bool timestamp_synced = false;
  bool tai_offset_applied = false;
};

struct DatasetPlaybackLidarPoint {
  int32_t x_mm = 0;
  int32_t y_mm = 0;
  int32_t z_mm = 0;
  uint8_t reflectivity = 0;
  uint8_t tag = 0;
};

struct DatasetPlaybackLidarImuSample {
  uint64_t timestamp_us = 0;
  std::array<double, 3> acceleration_m_s2{};
  std::array<double, 3> angular_velocity_rad_s{};
  uint8_t model = 0;
  uint8_t device_type = 0;
  uint8_t time_type = 0;
  uint32_t sample_id = 0;
  uint64_t timestamp_raw_ns = 0;
  bool timestamp_synced = false;
  bool tai_offset_applied = false;
  bool has_time_source = false;
};

struct DatasetPlaybackEvent {
  uint64_t timestamp_us = 0;
  DatasetPlaybackEventType type = DatasetPlaybackEventType::CameraFrame;
  size_t stream_index = 0;
};

struct DatasetPlaybackData {
  std::array<std::vector<DatasetPlaybackImuSample>, 2> onboard_imus;
  std::vector<DatasetPlaybackLidarBatch> lidar_batches;
  std::vector<DatasetPlaybackLidarImuSample> lidar_imu_samples;
  std::vector<DatasetPlaybackEvent> timeline;

  bool empty() const noexcept { return timeline.empty(); }
};

bool loadDatasetPlaybackData(
    const std::filesystem::path& root,
    const std::vector<uint64_t>& camera_frame_timestamps_us,
    DatasetPlaybackData* data, std::string* error);

bool loadDatasetLidarPoints(
    const DatasetPlaybackLidarBatch& batch,
    std::vector<DatasetPlaybackLidarPoint>* points, std::string* error);

size_t firstDatasetPlaybackEventAfter(
    const DatasetPlaybackData& data, uint64_t timestamp_us);

}  // namespace prism_viewer::dataset
