#include "dataset/imu_time_alignment.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using Vector3 = std::array<double, 3>;
using Matrix3 = std::array<double, 9>;

Vector3 motion(double seconds) {
  constexpr double kPi = 3.14159265358979323846;
  return {
      1.7 * std::sin(2.0 * kPi * 2.71 * seconds) +
          0.8 * std::sin(2.0 * kPi * 17.13 * seconds),
      1.3 * std::cos(2.0 * kPi * 4.37 * seconds) +
          0.7 * std::sin(2.0 * kPi * 23.17 * seconds),
      1.1 * std::sin(2.0 * kPi * 7.19 * seconds + 0.4) +
          0.5 * std::cos(2.0 * kPi * 31.43 * seconds),
  };
}

Matrix3 mountingRotation() {
  constexpr double kPi = 3.14159265358979323846;
  const double roll = 15.0 * kPi / 180.0;
  const double pitch = -20.0 * kPi / 180.0;
  const double yaw = 35.0 * kPi / 180.0;
  const double cr = std::cos(roll);
  const double sr = std::sin(roll);
  const double cp = std::cos(pitch);
  const double sp = std::sin(pitch);
  const double cy = std::cos(yaw);
  const double sy = std::sin(yaw);
  return {
      cy * cp, cy * sp * sr - sy * cr, cy * sp * cr + sy * sr,
      sy * cp, sy * sp * sr + cy * cr, sy * sp * cr - cy * sr,
      -sp, cp * sr, cp * cr,
  };
}

Vector3 transposeMultiply(const Matrix3& matrix, const Vector3& vector) {
  return {
      matrix[0] * vector[0] + matrix[3] * vector[1] + matrix[6] * vector[2],
      matrix[1] * vector[0] + matrix[4] * vector[1] + matrix[7] * vector[2],
      matrix[2] * vector[0] + matrix[5] * vector[1] + matrix[8] * vector[2],
  };
}

void writeTimestamp(std::ostream& output, uint64_t timestamp_us) {
  output << timestamp_us / 1000000ULL << '.' << std::setw(6)
         << std::setfill('0') << timestamp_us % 1000000ULL;
}

void createSyntheticDataset(const std::filesystem::path& root,
                            int64_t offset_us, bool moving) {
  std::filesystem::create_directories(root);
  std::ofstream imu0(root / "imu0.tum");
  std::ofstream lidar(root / "lidar_imu.tum");
  imu0.imbue(std::locale::classic());
  lidar.imbue(std::locale::classic());
  imu0 << std::fixed << std::setprecision(9);
  lidar << std::fixed << std::setprecision(9);
  constexpr uint64_t kStartUs = 1780000000000000ULL;
  const Matrix3 rotation = mountingRotation();
  for (uint64_t index = 0; index <= 13000u; ++index) {
    const uint64_t physical_us = kStartUs + index * 1000u;
    const Vector3 source = moving ? motion(index / 1000.0) : Vector3{};
    writeTimestamp(imu0, physical_us);
    imu0 << " 0 0 9.80665 " << source[0] << ' ' << source[1] << ' '
         << source[2] << '\n';
    if (index % 5u == 0u) {
      const Vector3 rotated = transposeMultiply(rotation, source);
      writeTimestamp(lidar, physical_us + offset_us);
      lidar << " 0 0 9.80665 " << rotated[0] << ' ' << rotated[1] << ' '
            << rotated[2] << " 1 9 1 " << index / 5u << ' '
            << (physical_us + offset_us) * 1000u << " 1 1\n";
    }
  }
  if (!imu0.good() || !lidar.good()) {
    throw std::runtime_error("failed to create synthetic dataset");
  }
}

void verifyOffset(const std::filesystem::path& root, int64_t offset_us) {
  createSyntheticDataset(root, offset_us, true);
  const auto result =
      prism_viewer::dataset::analyzeImu0LidarImuTimeOffset(root);
  if (!result.valid || std::abs(result.offset_us - offset_us) > 40.0 ||
      result.correlation < 0.98 || result.analyzed_duration_s < 10.0) {
    std::cerr << "offset test failed: injected=" << offset_us
              << " estimated=" << result.offset_us
              << " correlation=" << result.correlation
              << " peak_width=" << result.correlation_peak_width_us
              << " message=" << result.message << '\n';
    throw std::runtime_error("synthetic offset was not recovered");
  }
}

}  // namespace

int main() {
  const auto nonce =
      std::chrono::steady_clock::now().time_since_epoch().count();
  const auto root = std::filesystem::temp_directory_path() /
                    ("prism-imu-alignment-test-" + std::to_string(nonce));
  try {
    const auto missing =
        prism_viewer::dataset::analyzeImu0LidarImuTimeOffset(root);
    if (missing.status !=
        prism_viewer::dataset::ImuTimeAlignmentStatus::MissingInput) {
      throw std::runtime_error("missing inputs were not rejected");
    }
    verifyOffset(root / "positive", 1370);
    verifyOffset(root / "negative", -2840);
    const auto stationary_root = root / "stationary";
    createSyntheticDataset(stationary_root, 1200, false);
    const auto stationary =
        prism_viewer::dataset::analyzeImu0LidarImuTimeOffset(stationary_root);
    if (stationary.valid || stationary.status !=
                                prism_viewer::dataset::
                                    ImuTimeAlignmentStatus::InsufficientMotion) {
      throw std::runtime_error("stationary data produced a false offset");
    }
    std::filesystem::remove_all(root);
    std::cout << "IMU0/LiDAR IMU time alignment tests passed\n";
    return EXIT_SUCCESS;
  } catch (const std::exception& exception) {
    std::filesystem::remove_all(root);
    std::cerr << exception.what() << '\n';
    return EXIT_FAILURE;
  }
}
