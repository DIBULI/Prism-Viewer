#include "dataset/dataset_playback.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace prism_viewer::dataset {
namespace {

constexpr uint64_t kStoredLidarPointBytes = 16u;
constexpr uint32_t kMaximumLidarPointsPerBatch = 1000000u;

uint64_t parseTimestampUs(const std::string& token) {
  if (token.empty() || token.front() == '-') {
    throw std::runtime_error("timestamp is empty or negative");
  }
  const size_t decimal = token.find('.');
  if (decimal != std::string::npos &&
      token.find('.', decimal + 1u) != std::string::npos) {
    throw std::runtime_error("timestamp has multiple decimal points");
  }
  const std::string seconds =
      decimal == std::string::npos ? token : token.substr(0, decimal);
  std::string fraction = decimal == std::string::npos
                             ? std::string()
                             : token.substr(decimal + 1u);
  const auto decimal_digit = [](char value) {
    return value >= '0' && value <= '9';
  };
  if (seconds.empty() ||
      !std::all_of(seconds.begin(), seconds.end(), decimal_digit) ||
      !std::all_of(fraction.begin(), fraction.end(), decimal_digit)) {
    throw std::runtime_error("timestamp contains non-decimal characters");
  }
  if (fraction.size() > 6u) fraction.resize(6u);
  while (fraction.size() < 6u) fraction.push_back('0');
  const uint64_t seconds_value = std::stoull(seconds);
  const uint64_t fraction_value =
      fraction.empty() ? 0u : std::stoull(fraction);
  if (seconds_value >
      (std::numeric_limits<uint64_t>::max() - fraction_value) / 1000000u) {
    throw std::runtime_error("timestamp overflows microseconds");
  }
  return seconds_value * 1000000u + fraction_value;
}

bool isSafeRelativePath(const std::filesystem::path& path) {
  if (path.empty() || path.is_absolute()) return false;
  for (const auto& component : path) {
    if (component == std::filesystem::path("..")) return false;
  }
  return true;
}

void requireMonotonic(uint64_t timestamp_us, uint64_t previous_timestamp_us,
                      uint64_t row, const std::string& filename) {
  if (row != 0u && timestamp_us <= previous_timestamp_us) {
    throw std::runtime_error(filename +
                             " timestamps are duplicate or not monotonic");
  }
}

void loadOnboardImu(const std::filesystem::path& root, size_t sensor,
                    std::vector<DatasetPlaybackImuSample>* samples) {
  const std::string filename = "imu" + std::to_string(sensor) + ".tum";
  const std::filesystem::path path = root / filename;
  if (!std::filesystem::is_regular_file(path)) return;

  std::ifstream input(path);
  if (!input.is_open()) throw std::runtime_error("cannot open " + filename);
  uint64_t line_number = 0;
  uint64_t previous_timestamp_us = 0;
  for (std::string line; std::getline(input, line);) {
    ++line_number;
    if (line.empty() || line.front() == '#') continue;
    std::istringstream parser(line);
    std::string timestamp_text;
    DatasetPlaybackImuSample sample;
    sample.sensor_id = static_cast<uint8_t>(sensor);
    std::string trailing;
    if (!(parser >> timestamp_text >> sample.acceleration_m_s2[0] >>
          sample.acceleration_m_s2[1] >> sample.acceleration_m_s2[2] >>
          sample.angular_velocity_rad_s[0] >>
          sample.angular_velocity_rad_s[1] >>
          sample.angular_velocity_rad_s[2]) ||
        (parser >> trailing)) {
      throw std::runtime_error("invalid " + filename + " line " +
                               std::to_string(line_number));
    }
    const bool finite =
        std::all_of(sample.acceleration_m_s2.begin(),
                    sample.acceleration_m_s2.end(),
                    [](double value) { return std::isfinite(value); }) &&
        std::all_of(sample.angular_velocity_rad_s.begin(),
                    sample.angular_velocity_rad_s.end(),
                    [](double value) { return std::isfinite(value); });
    if (!finite) {
      throw std::runtime_error("non-finite value in " + filename + " line " +
                               std::to_string(line_number));
    }
    sample.timestamp_us = parseTimestampUs(timestamp_text);
    requireMonotonic(sample.timestamp_us, previous_timestamp_us,
                     samples->size(), filename);
    previous_timestamp_us = sample.timestamp_us;
    samples->push_back(sample);
  }
  if (!input.eof()) throw std::runtime_error("cannot read " + filename);
}

void loadLidarIndex(const std::filesystem::path& root,
                    std::vector<DatasetPlaybackLidarBatch>* batches) {
  constexpr const char* kFilename = "lidar.tum";
  const std::filesystem::path path = root / kFilename;
  if (!std::filesystem::is_regular_file(path)) return;

  std::ifstream input(path);
  if (!input.is_open()) throw std::runtime_error("cannot open lidar.tum");
  uint64_t line_number = 0;
  uint64_t previous_timestamp_us = 0;
  for (std::string line; std::getline(input, line);) {
    ++line_number;
    if (line.empty() || line.front() == '#') continue;
    std::istringstream parser(line);
    std::string timestamp_text;
    std::string relative_path_text;
    uint64_t byte_size = 0;
    uint64_t point_count = 0;
    uint32_t model = 0;
    uint32_t device_type = 0;
    uint32_t time_type = 0;
    uint64_t batch_id = 0;
    DatasetPlaybackLidarBatch batch;
    if (!(parser >> timestamp_text >> relative_path_text >> batch.byte_offset >>
          byte_size >> point_count >> model >> device_type >> time_type >>
          batch_id >> batch.timestamp_raw) ||
        byte_size > std::numeric_limits<uint32_t>::max() ||
        point_count == 0u || point_count > kMaximumLidarPointsPerBatch ||
        byte_size != point_count * kStoredLidarPointBytes ||
        (model != 1u && model != 2u) || device_type > 255u ||
        time_type > 255u || batch_id > std::numeric_limits<uint32_t>::max()) {
      throw std::runtime_error("invalid lidar.tum line " +
                               std::to_string(line_number));
    }
    parser >> std::ws;
    if (!parser.eof()) {
      uint32_t interval = 0;
      uint32_t synced = 0;
      uint32_t tai = 0;
      std::string trailing;
      if (!(parser >> interval >> synced >> tai) || (parser >> trailing) ||
          interval > std::numeric_limits<uint16_t>::max() || synced > 1u ||
          tai > 1u || (tai != 0u && synced == 0u)) {
        throw std::runtime_error("invalid lidar.tum time source at line " +
                                 std::to_string(line_number));
      }
      batch.time_interval_100ns = static_cast<uint16_t>(interval);
      batch.timestamp_synced = synced != 0u;
      batch.tai_offset_applied = tai != 0u;
    }

    const std::filesystem::path relative_path(relative_path_text);
    if (!isSafeRelativePath(relative_path)) {
      throw std::runtime_error("unsafe lidar container path at line " +
                               std::to_string(line_number));
    }
    batch.container_path = (root / relative_path).lexically_normal();
    std::error_code filesystem_error;
    const uint64_t container_size =
        std::filesystem::file_size(batch.container_path, filesystem_error);
    if (filesystem_error ||
        batch.byte_offset > std::numeric_limits<uint64_t>::max() - byte_size ||
        batch.byte_offset + byte_size > container_size) {
      throw std::runtime_error("invalid lidar container range at line " +
                               std::to_string(line_number));
    }
    batch.timestamp_us = parseTimestampUs(timestamp_text);
    requireMonotonic(batch.timestamp_us, previous_timestamp_us,
                     batches->size(), kFilename);
    previous_timestamp_us = batch.timestamp_us;
    batch.byte_size = static_cast<uint32_t>(byte_size);
    batch.point_count = static_cast<uint32_t>(point_count);
    batch.model = static_cast<uint8_t>(model);
    batch.device_type = static_cast<uint8_t>(device_type);
    batch.time_type = static_cast<uint8_t>(time_type);
    batch.batch_id = static_cast<uint32_t>(batch_id);
    batches->push_back(std::move(batch));
  }
  if (!input.eof()) throw std::runtime_error("cannot read lidar.tum");
}

void loadLidarImu(
    const std::filesystem::path& root,
    std::vector<DatasetPlaybackLidarImuSample>* samples) {
  constexpr const char* kFilename = "lidar_imu.tum";
  const std::filesystem::path path = root / kFilename;
  if (!std::filesystem::is_regular_file(path)) return;

  std::ifstream input(path);
  if (!input.is_open()) throw std::runtime_error("cannot open lidar_imu.tum");
  uint64_t line_number = 0;
  uint64_t previous_timestamp_us = 0;
  for (std::string line; std::getline(input, line);) {
    ++line_number;
    if (line.empty() || line.front() == '#') continue;
    std::istringstream parser(line);
    std::string timestamp_text;
    DatasetPlaybackLidarImuSample sample;
    if (!(parser >> timestamp_text >> sample.acceleration_m_s2[0] >>
          sample.acceleration_m_s2[1] >> sample.acceleration_m_s2[2] >>
          sample.angular_velocity_rad_s[0] >>
          sample.angular_velocity_rad_s[1] >>
          sample.angular_velocity_rad_s[2])) {
      throw std::runtime_error("invalid lidar_imu.tum line " +
                               std::to_string(line_number));
    }
    const bool finite =
        std::all_of(sample.acceleration_m_s2.begin(),
                    sample.acceleration_m_s2.end(),
                    [](double value) { return std::isfinite(value); }) &&
        std::all_of(sample.angular_velocity_rad_s.begin(),
                    sample.angular_velocity_rad_s.end(),
                    [](double value) { return std::isfinite(value); });
    if (!finite) {
      throw std::runtime_error("non-finite value in lidar_imu.tum line " +
                               std::to_string(line_number));
    }

    parser >> std::ws;
    if (!parser.eof()) {
      uint32_t model = 0;
      uint32_t device_type = 0;
      uint32_t time_type = 0;
      uint64_t sample_id = 0;
      uint32_t synced = 0;
      if (!(parser >> model >> device_type >> time_type >> sample_id >>
            sample.timestamp_raw_ns >> synced) ||
          (model != 1u && model != 2u) || device_type > 255u ||
          time_type > 255u ||
          sample_id > std::numeric_limits<uint32_t>::max() || synced > 1u) {
        throw std::runtime_error("invalid lidar_imu.tum source at line " +
                                 std::to_string(line_number));
      }
      sample.has_time_source = true;
      sample.model = static_cast<uint8_t>(model);
      sample.device_type = static_cast<uint8_t>(device_type);
      sample.time_type = static_cast<uint8_t>(time_type);
      sample.sample_id = static_cast<uint32_t>(sample_id);
      sample.timestamp_synced = synced != 0u;
      parser >> std::ws;
      if (!parser.eof()) {
        uint32_t tai = 0;
        std::string trailing;
        if (!(parser >> tai) || (parser >> trailing) || tai > 1u ||
            (tai != 0u && !sample.timestamp_synced)) {
          throw std::runtime_error("invalid lidar_imu.tum TAI flag at line " +
                                   std::to_string(line_number));
        }
        sample.tai_offset_applied = tai != 0u;
      }
    }

    sample.timestamp_us = parseTimestampUs(timestamp_text);
    requireMonotonic(sample.timestamp_us, previous_timestamp_us,
                     samples->size(), kFilename);
    previous_timestamp_us = sample.timestamp_us;
    samples->push_back(sample);
  }
  if (!input.eof()) throw std::runtime_error("cannot read lidar_imu.tum");
}

int eventPriority(DatasetPlaybackEventType type) {
  switch (type) {
    case DatasetPlaybackEventType::OnboardImu0:
      return 0;
    case DatasetPlaybackEventType::OnboardImu1:
      return 1;
    case DatasetPlaybackEventType::CameraFrame:
      return 2;
    case DatasetPlaybackEventType::LidarPoints:
      return 3;
    case DatasetPlaybackEventType::LidarImu:
      return 4;
  }
  return 5;
}

uint32_t readLittleEndianU32(const uint8_t* bytes) {
  return static_cast<uint32_t>(bytes[0]) |
         (static_cast<uint32_t>(bytes[1]) << 8u) |
         (static_cast<uint32_t>(bytes[2]) << 16u) |
         (static_cast<uint32_t>(bytes[3]) << 24u);
}

}  // namespace

bool loadDatasetPlaybackData(
    const std::filesystem::path& root,
    const std::vector<uint64_t>& camera_frame_timestamps_us,
    DatasetPlaybackData* data, std::string* error) {
  if (data == nullptr) {
    if (error != nullptr) *error = "playback output is required";
    return false;
  }
  *data = {};
  if (error != nullptr) error->clear();
  try {
    for (size_t index = 1u; index < camera_frame_timestamps_us.size();
         ++index) {
      if (camera_frame_timestamps_us[index] <=
          camera_frame_timestamps_us[index - 1u]) {
        throw std::runtime_error(
            "camera timestamps are duplicate or not monotonic");
      }
    }
    for (size_t sensor = 0; sensor < data->onboard_imus.size(); ++sensor) {
      loadOnboardImu(root, sensor, &data->onboard_imus[sensor]);
    }
    loadLidarIndex(root, &data->lidar_batches);
    loadLidarImu(root, &data->lidar_imu_samples);

    data->timeline.reserve(
        camera_frame_timestamps_us.size() + data->onboard_imus[0].size() +
        data->onboard_imus[1].size() + data->lidar_batches.size() +
        data->lidar_imu_samples.size());
    for (size_t index = 0; index < camera_frame_timestamps_us.size(); ++index) {
      data->timeline.push_back({camera_frame_timestamps_us[index],
                                DatasetPlaybackEventType::CameraFrame, index});
    }
    for (size_t sensor = 0; sensor < data->onboard_imus.size(); ++sensor) {
      const auto type = sensor == 0u
                            ? DatasetPlaybackEventType::OnboardImu0
                            : DatasetPlaybackEventType::OnboardImu1;
      for (size_t index = 0; index < data->onboard_imus[sensor].size();
           ++index) {
        data->timeline.push_back(
            {data->onboard_imus[sensor][index].timestamp_us, type, index});
      }
    }
    for (size_t index = 0; index < data->lidar_batches.size(); ++index) {
      data->timeline.push_back({data->lidar_batches[index].timestamp_us,
                                DatasetPlaybackEventType::LidarPoints, index});
    }
    for (size_t index = 0; index < data->lidar_imu_samples.size(); ++index) {
      data->timeline.push_back({data->lidar_imu_samples[index].timestamp_us,
                                DatasetPlaybackEventType::LidarImu, index});
    }
    std::stable_sort(
        data->timeline.begin(), data->timeline.end(),
        [](const DatasetPlaybackEvent& left,
           const DatasetPlaybackEvent& right) {
          if (left.timestamp_us != right.timestamp_us) {
            return left.timestamp_us < right.timestamp_us;
          }
          return eventPriority(left.type) < eventPriority(right.type);
        });
    return true;
  } catch (const std::exception& exception) {
    *data = {};
    if (error != nullptr) *error = exception.what();
    return false;
  }
}

bool loadDatasetLidarPoints(
    const DatasetPlaybackLidarBatch& batch,
    std::vector<DatasetPlaybackLidarPoint>* points, std::string* error) {
  if (points == nullptr) {
    if (error != nullptr) *error = "LiDAR point output is required";
    return false;
  }
  points->clear();
  if (error != nullptr) error->clear();
  try {
    if (batch.point_count == 0u ||
        batch.byte_size != batch.point_count * kStoredLidarPointBytes ||
        batch.byte_offset >
            static_cast<uint64_t>(std::numeric_limits<std::streamoff>::max())) {
      throw std::runtime_error("invalid LiDAR playback batch");
    }
    std::ifstream input(batch.container_path,
                        std::ios::in | std::ios::binary);
    if (!input.is_open()) {
      throw std::runtime_error("cannot open LiDAR container " +
                               batch.container_path.string());
    }
    input.seekg(static_cast<std::streamoff>(batch.byte_offset));
    std::vector<uint8_t> bytes(batch.byte_size);
    input.read(reinterpret_cast<char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    if (input.gcount() != static_cast<std::streamsize>(bytes.size())) {
      throw std::runtime_error("short read from LiDAR container " +
                               batch.container_path.string());
    }
    points->reserve(batch.point_count);
    for (size_t offset = 0; offset < bytes.size();
         offset += kStoredLidarPointBytes) {
      DatasetPlaybackLidarPoint point;
      point.x_mm = static_cast<int32_t>(readLittleEndianU32(&bytes[offset]));
      point.y_mm =
          static_cast<int32_t>(readLittleEndianU32(&bytes[offset + 4u]));
      point.z_mm =
          static_cast<int32_t>(readLittleEndianU32(&bytes[offset + 8u]));
      point.reflectivity = bytes[offset + 12u];
      point.tag = bytes[offset + 13u];
      points->push_back(point);
    }
    return true;
  } catch (const std::exception& exception) {
    points->clear();
    if (error != nullptr) *error = exception.what();
    return false;
  }
}

size_t firstDatasetPlaybackEventAfter(const DatasetPlaybackData& data,
                                      uint64_t timestamp_us) {
  return static_cast<size_t>(std::upper_bound(
      data.timeline.begin(), data.timeline.end(), timestamp_us,
      [](uint64_t timestamp, const DatasetPlaybackEvent& event) {
        return timestamp < event.timestamp_us;
      }) - data.timeline.begin());
}

}  // namespace prism_viewer::dataset
