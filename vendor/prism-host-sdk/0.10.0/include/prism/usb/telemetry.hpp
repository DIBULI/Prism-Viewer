#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "prism/usb/common.hpp"

namespace prism {

constexpr uint16_t kImuFlagFsyncEvent = 1u << 0;
constexpr uint16_t kImuFlagFsyncDelayValid = 1u << 1;
constexpr uint16_t kImuFlagSampleGap = 1u << 2;
constexpr uint16_t kImuFlagTimestampSynced = 1u << 7;
constexpr uint8_t kLidarPointFlagTimestampSynced = 1u << 0;
constexpr uint8_t kLidarPointFlagTaiOffsetApplied = 1u << 1;
constexpr uint8_t kLidarImuFlagTimestampSynced = 1u << 0;
constexpr uint8_t kLidarImuFlagTaiOffsetApplied = 1u << 1;

struct HeartbeatStatus {
  // RK CLOCK_REALTIME in Unix microseconds. Device status is deliberately
  // absent; query Client::deviceInfo() for status.
  uint64_t rk_system_time_us = 0;
};

struct NetworkInfo {
  uint16_t flags = 0;
  uint16_t interface_count = 0;
  std::string hostname;
  std::string primary_interface;
  std::string ipv4;
  std::string netmask;
  std::string gateway;
  std::string mac;
  std::string dns;
  std::string summary;
};

struct VideoStatus {
  bool enabled = false;
  uint8_t cameras = 0;
  uint16_t fps = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t payload_size = 0;
};

struct VideoChunk {
  uint8_t camera_id = 0;
  uint8_t format = 0;
  uint16_t flags = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t frame_id = 0;
  uint32_t encoded_size = 0;
  uint32_t chunk_offset = 0;
  uint32_t chunk_size = 0;
  uint64_t timestamp_us = 0;
  std::vector<uint8_t> data;
};

// Non-owning view over a VIDEO_CHUNK frame. The view remains valid only while
// the source Frame (and its payload) is alive. Use this on high-rate receive
// paths to avoid copying every JPEG chunk into a second vector.
struct VideoChunkView {
  uint8_t camera_id = 0;
  uint8_t format = 0;
  uint16_t flags = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t frame_id = 0;
  uint32_t encoded_size = 0;
  uint32_t chunk_offset = 0;
  uint32_t chunk_size = 0;
  uint64_t timestamp_us = 0;
  const uint8_t* data = nullptr;
  size_t data_size = 0;
};

struct VideoMeta {
  bool valid = false;
  uint8_t cameras = 0;
  uint32_t host_frame_id = 0;
  uint32_t carrier_frame_id = 0;
  uint32_t carrier_width_bytes = 0;
  uint32_t image_height_per_camera = 0;
  uint32_t meta_row_bytes = 0;
  uint64_t trigger_time_ns = 0;
  std::array<uint32_t, 4> exposure_us{};
  std::array<uint32_t, 4> analog_gain_x1024{};
  std::array<uint32_t, 4> digital_gain_x1024{};
  uint32_t meta_crc32 = 0;
};

struct ImuSample {
  uint8_t sensor_id = 0;
  uint8_t format = 0;
  uint16_t flags = 0;
  uint32_t sample_id = 0;
  uint64_t timestamp_us = 0;  // Unix UTC when flags bit 7 is set; otherwise sensor-board local.
  bool fsync_event = false;        // First ODR sample after an IMU FSYNC edge.
  bool fsync_delay_valid = false;  // ICM-42688 delay field was not 0xffff.
  bool sample_gap = false;         // Raw IMU timestamp exposed a >4 ODR gap.
  bool timestamp_synced = false;   // timestamp_us is synchronized UTC.
  std::array<int32_t, 3> accel_mg{};
  std::array<int32_t, 3> gyro_mdps{};
  int32_t temp_milli_c = 0;
};

struct ImuStreamStatus {
  bool enabled = false;
  uint8_t sensors = 0;
  uint16_t nominal_rate_hz = 0;
  uint32_t payload_size = 0;
};

struct LidarStatus {
  uint16_t version = 0;
  bool available = false;
  bool enabled = false;
  bool connected = false;
  bool receiving = false;
  LidarModel model = LidarModel::None;
  uint8_t device_type = 0;
  uint32_t handle = 0;
  uint64_t packet_count = 0;
  uint64_t point_count = 0;
  uint64_t dropped_point_count = 0;
  std::string serial;
  std::string lidar_ip;
  std::string error;
};

struct LidarNetworkConfiguration {
  bool enabled = true;
  std::string host_ip = "192.168.1.5";
  std::string netmask = "255.255.255.0";
  std::string lidar_ip = "192.168.1.3";
};

struct LidarNetworkStatus {
  uint16_t version = 0;
  LidarNetworkConfiguration configuration;
  bool interface_present = false;
  bool link_up = false;
  bool address_applied = false;
  bool same_subnet = false;
  bool target_reachable = false;
  bool persisted = false;
  int32_t error_code = 0;
  uint32_t generation = 0;
  std::string interface_name;
  std::string error;
};

struct LidarPoint {
  int32_t x_mm = 0;
  int32_t y_mm = 0;
  int32_t z_mm = 0;
  uint8_t reflectivity = 0;
  uint8_t tag = 0;
};

struct LidarPointBatch {
  uint16_t version = 0;
  LidarModel model = LidarModel::None;
  uint8_t device_type = 0;
  uint8_t time_type = 0;
  uint8_t flags = 0;
  bool timestamp_synced = false;
  bool tai_offset_applied = false;
  uint32_t handle = 0;
  uint32_t batch_id = 0;
  uint64_t timestamp_raw = 0;  // Unmodified Livox packet timestamp.
  // Batch-base time in the RK CLOCK_REALTIME microsecond domain. When
  // timestamp_synced is false this is callback arrival time (or zero if the
  // RK clock could not be read).
  uint64_t timestamp_utc_us = 0;
  // Total first-to-last point span in 100 ns units. It is not per-point
  // spacing; the SDK does not expand point timestamps or deskew the batch.
  uint16_t time_interval_100ns = 0;
  std::vector<LidarPoint> points;
};

struct LidarImuSample {
  uint16_t version = 0;
  LidarModel model = LidarModel::None;
  uint8_t device_type = 0;
  uint8_t time_type = 0;
  uint8_t flags = 0;
  bool timestamp_synced = false;
  bool tai_offset_applied = false;
  uint32_t handle = 0;
  uint32_t sample_id = 0;
  uint64_t timestamp_raw_ns = 0;
  uint64_t timestamp_utc_us = 0;
  std::array<float, 3> gyro_rad_s{};
  std::array<float, 3> accel_m_s2{};
};

HeartbeatStatus parseHeartbeat(const Frame& frame);
VideoChunkView parseVideoChunkView(const Frame& frame);
VideoChunk parseVideoChunk(const Frame& frame);
VideoMeta parseVideoMeta(const Frame& frame);
ImuSample parseImuSample(const Frame& frame);
LidarStatus parseLidarStatus(const Frame& frame);
LidarNetworkStatus parseLidarNetworkStatus(const Frame& frame);
LidarPointBatch parseLidarPointBatch(const Frame& frame);
LidarImuSample parseLidarImuSample(const Frame& frame);

}  // namespace prism
