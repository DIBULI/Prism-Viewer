#include "dataset/dataset_playback.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

void writeText(const std::filesystem::path& path, const std::string& text) {
  std::ofstream output(path, std::ios::out | std::ios::trunc);
  output << text;
  if (!output.good()) throw std::runtime_error("cannot write test text");
}

void appendU32(std::vector<uint8_t>* bytes, uint32_t value) {
  for (unsigned shift = 0; shift < 32u; shift += 8u) {
    bytes->push_back(static_cast<uint8_t>((value >> shift) & 0xffu));
  }
}

void appendPoint(std::vector<uint8_t>* bytes, int32_t x, int32_t y,
                 int32_t z, uint8_t reflectivity, uint8_t tag) {
  appendU32(bytes, static_cast<uint32_t>(x));
  appendU32(bytes, static_cast<uint32_t>(y));
  appendU32(bytes, static_cast<uint32_t>(z));
  bytes->push_back(reflectivity);
  bytes->push_back(tag);
  bytes->push_back(0u);
  bytes->push_back(0u);
}

void writeBytes(const std::filesystem::path& path,
                const std::vector<uint8_t>& bytes) {
  std::ofstream output(path, std::ios::out | std::ios::binary |
                                 std::ios::trunc);
  output.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  if (!output.good()) throw std::runtime_error("cannot write test bytes");
}

}  // namespace

int main() {
  using prism_viewer::dataset::DatasetPlaybackData;
  using prism_viewer::dataset::DatasetPlaybackEventType;
  using prism_viewer::dataset::DatasetPlaybackLidarPoint;
  using prism_viewer::dataset::firstDatasetPlaybackEventAfter;
  using prism_viewer::dataset::loadDatasetLidarPoints;
  using prism_viewer::dataset::loadDatasetPlaybackData;

  const auto nonce =
      std::chrono::steady_clock::now().time_since_epoch().count();
  const std::filesystem::path root =
      std::filesystem::temp_directory_path() /
      ("prism-dataset-playback-test-" + std::to_string(nonce));
  std::filesystem::create_directories(root);
  try {
    writeText(root / "imu0.tum",
              "# imu0\n"
              "1780000000.000000 0.1 -0.2 9.8 0.01 -0.02 0.03\n"
              "1780000000.001000 0.4 0.5 9.7 0.04 0.05 0.06\n");
    writeText(root / "imu1.tum",
              "1780000000.000500 -0.1 0.2 9.6 -0.01 0.02 -0.03\n");

    std::vector<uint8_t> lidar_bytes;
    appendPoint(&lidar_bytes, 1000, -2000, 3000, 77u, 4u);
    appendPoint(&lidar_bytes, -4000, 5000, -6000, 88u, 5u);
    writeBytes(root / "lidar-data-0000.bin", lidar_bytes);
    writeText(root / "lidar.tum",
              "1780000000.000700 lidar-data-0000.bin 0 32 2 2 35 1 9 "
              "1780000037000700000 100 1 1\n");
    writeText(root / "lidar_imu.tum",
              "1780000000.000800 1.25 -2.5 9.80665 0.125 -0.25 0.5 "
              "2 35 1 10 1780000037000800000 1 1\n");

    DatasetPlaybackData data;
    std::string error;
    require(loadDatasetPlaybackData(root, {1780000000000600u}, &data,
                                    &error),
            error.c_str());
    require(data.onboard_imus[0].size() == 2u &&
                data.onboard_imus[1].size() == 1u,
            "onboard IMU streams were not loaded");
    require(data.lidar_batches.size() == 1u &&
                data.lidar_imu_samples.size() == 1u,
            "LiDAR streams were not loaded");
    require(data.timeline.size() == 6u, "timeline event count is wrong");
    require(data.timeline[0].type == DatasetPlaybackEventType::OnboardImu0 &&
                data.timeline[1].type ==
                    DatasetPlaybackEventType::OnboardImu1 &&
                data.timeline[2].type == DatasetPlaybackEventType::CameraFrame &&
                data.timeline[3].type == DatasetPlaybackEventType::LidarPoints &&
                data.timeline[4].type == DatasetPlaybackEventType::LidarImu &&
                data.timeline[5].type == DatasetPlaybackEventType::OnboardImu0,
            "timeline is not timestamp ordered");
    require(firstDatasetPlaybackEventAfter(data, 1780000000000700u) == 4u,
            "timeline upper-bound seek is wrong");

    DatasetPlaybackData duplicate_camera;
    require(!loadDatasetPlaybackData(
                root, {1780000000000600u, 1780000000000600u},
                &duplicate_camera, &error) &&
                error.find("camera timestamps") != std::string::npos,
            "duplicate camera timestamp was accepted");

    std::vector<DatasetPlaybackLidarPoint> points;
    require(loadDatasetLidarPoints(data.lidar_batches.front(), &points,
                                   &error),
            error.c_str());
    require(points.size() == 2u && points[0].x_mm == 1000 &&
                points[0].y_mm == -2000 && points[0].z_mm == 3000 &&
                points[0].reflectivity == 77u && points[0].tag == 4u &&
                points[1].x_mm == -4000 && points[1].z_mm == -6000,
            "LiDAR point payload was decoded incorrectly");

    writeText(root / "imu1.tum",
              "1780000000.000500 0 0 0 0 0 0\n"
              "1780000000.000500 0 0 0 0 0 0\n");
    DatasetPlaybackData duplicate_imu;
    require(!loadDatasetPlaybackData(root, {}, &duplicate_imu, &error) &&
                error.find("duplicate") != std::string::npos,
            "duplicate IMU timestamp was accepted");
    writeText(root / "imu1.tum",
              "1780000000.000500 -0.1 0.2 9.6 -0.01 0.02 -0.03\n");

    writeText(root / "lidar.tum",
              "1780000000.000700 ../outside.bin 0 16 1 1 35 1 1 0\n");
    DatasetPlaybackData unsafe;
    require(!loadDatasetPlaybackData(root, {}, &unsafe, &error) &&
                error.find("unsafe") != std::string::npos,
            "unsafe LiDAR path was accepted");

    writeText(root / "lidar.tum",
              "1780000000.000700 lidar-data-0000.bin 24 16 1 1 35 1 1 0\n");
    DatasetPlaybackData invalid_range;
    require(!loadDatasetPlaybackData(root, {}, &invalid_range, &error) &&
                error.find("range") != std::string::npos,
            "out-of-range LiDAR payload was accepted");

    std::filesystem::remove_all(root);
    std::cout << "Dataset playback tests passed\n";
    return 0;
  } catch (const std::exception& exception) {
    std::filesystem::remove_all(root);
    std::cerr << exception.what() << '\n';
    return 1;
  }
}
