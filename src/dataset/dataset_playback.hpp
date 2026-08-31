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
  GpsRtk,
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

struct DatasetPlaybackGpsRtkSample {
  uint64_t timestamp_us = 0;
  uint16_t solution = 0;
  uint16_t confidence = 0;
  uint16_t satellites = 0;
  uint16_t confidence_score = 0;
  uint32_t confidence_reasons = 0;
  uint16_t base_source = 0;
  int32_t base_station_id = 0;
  uint32_t consecutive_fix_epochs = 0;
  uint32_t consecutive_float_epochs = 0;
  double latitude_deg = 0.0;
  double longitude_deg = 0.0;
  double ellipsoidal_height_m = 0.0;
  double east_std_m = 0.0;
  double north_std_m = 0.0;
  double up_std_m = 0.0;
  double differential_age_s = 0.0;
  double ambiguity_ratio = 0.0;
  double position_jump_m = 0.0;
  uint64_t solution_count = 0;
  uint64_t fix_count = 0;
  uint64_t float_count = 0;
  uint64_t rover_observation_epochs = 0;
  uint64_t base_observation_epochs = 0;
  uint64_t decoder_errors = 0;
  bool confidence_valid = false;
  bool position_jump_valid = false;
  bool base_position_valid = false;
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
  std::vector<DatasetPlaybackGpsRtkSample> gps_rtk_samples;
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
