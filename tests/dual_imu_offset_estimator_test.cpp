#include "dual_imu_offset_estimator.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

using Vector3 = std::array<double, 3>;
using Matrix3 = std::array<double, 9>;

Vector3 motionSignal(double seconds) {
  constexpr double kPi = 3.14159265358979323846;
  return {
      95.0 * std::sin(2.0 * kPi * 2.71 * seconds) +
          42.0 * std::sin(2.0 * kPi * 17.13 * seconds),
      71.0 * std::cos(2.0 * kPi * 4.37 * seconds) +
          33.0 * std::sin(2.0 * kPi * 23.17 * seconds),
      63.0 * std::sin(2.0 * kPi * 7.19 * seconds + 0.4) +
          28.0 * std::cos(2.0 * kPi * 31.43 * seconds),
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

prism_viewer::ImuOffsetEstimate runSynthetic(int64_t offset_us) {
  prism_viewer::DualImuOffsetEstimator estimator;
  estimator.start();
  constexpr uint64_t kStartUs = 1700000000000000ULL;
  const Matrix3 rotation = mountingRotation();
  for (uint64_t index = 0; index < 10300 && estimator.active(); ++index) {
    const uint64_t physical_us = kStartUs + index * 1000;
    const Vector3 imu0 = motionSignal(static_cast<double>(index) / 1000.0);
    const Vector3 imu1 = transposeMultiply(rotation, imu0);
    for (uint8_t sensor = 0; sensor < 2; ++sensor) {
      prism_viewer::ImuOffsetInput input;
      input.sensor_id = sensor;
      input.timestamp_us = physical_us + (sensor == 1 ? offset_us : 0);
      input.timestamp_synced = true;
      const Vector3& gyro = sensor == 0 ? imu0 : imu1;
      for (size_t axis = 0; axis < 3; ++axis) {
        input.gyro_mdps[axis] =
            static_cast<int32_t>(std::llround(gyro[axis] * 1000.0));
      }
      estimator.add(input);
    }
  }
  return estimator.latest();
}

bool verifyOffsetAndMounting(int64_t injected_offset_us) {
  const auto result = runSynthetic(injected_offset_us);
  const Matrix3 expected_rotation = mountingRotation();
  double maximum_rotation_error = 0.0;
  for (size_t element = 0; element < expected_rotation.size(); ++element) {
    maximum_rotation_error =
        std::max(maximum_rotation_error,
                 std::abs(result.imu1_to_imu0_rotation[element] -
                          expected_rotation[element]));
  }
  if (!result.valid || std::abs(result.offset_us - injected_offset_us) > 30.0 ||
      result.correlation < 0.99 || maximum_rotation_error > 0.01) {
    std::cerr << "offset/mounting test failed: injected=" << injected_offset_us
              << " estimated=" << result.offset_us
              << " correlation=" << result.correlation
              << " peak_width=" << result.correlation_peak_width_us
              << " rotation_error=" << maximum_rotation_error
              << " state=" << static_cast<int>(result.state) << '\n';
    return false;
  }
  return true;
}

}  // namespace

int main() {
  if (!verifyOffsetAndMounting(1370) || !verifyOffsetAndMounting(-840)) {
    return EXIT_FAILURE;
  }
  std::cout << "dual IMU offset and mounting estimator passed\n";
  return EXIT_SUCCESS;
}
