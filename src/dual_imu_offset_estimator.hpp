#pragma once

#include <array>
#include <cstdint>
#include <deque>

namespace prism_viewer {

enum class ImuOffsetState {
  Idle,
  WaitingForBothImus,
  WaitingForTimeSync,
  Collecting,
  Complete,
  InsufficientMotion,
  LowConfidence,
};

struct ImuOffsetInput {
  uint8_t sensor_id = 0;
  uint64_t timestamp_us = 0;
  bool timestamp_synced = false;
  std::array<int32_t, 3> gyro_mdps{};
};

struct ImuOffsetEstimate {
  bool updated = false;
  ImuOffsetState state = ImuOffsetState::Idle;
  double progress = 0.0;
  double duration_s = 0.0;
  std::array<uint64_t, 2> sample_count{};

  bool valid = false;
  // For the same physical rotation: timestamp_imu1 - timestamp_imu0.
  // Subtract this value from IMU1 timestamps to align them to IMU0.
  double offset_us = 0.0;
  double correlation = 0.0;
  double correlation_peak_width_us = 0.0;
  double motion_stddev_dps = 0.0;
  double motion_peak_to_peak_dps = 0.0;
  // Ratio of the second-largest to largest eigenvalue of the centered gyro
  // motion. Near zero means essentially single-axis motion, for which mounting
  // orientation cannot be determined uniquely.
  double orientation_excitation_ratio = 0.0;

  // Proper rotation that maps an IMU1 gyroscope vector into the IMU0 frame.
  // Row-major: gyro_imu0 ~= imu1_to_imu0_rotation * gyro_imu1.
  std::array<double, 9> imu1_to_imu0_rotation{
      1.0, 0.0, 0.0,
      0.0, 1.0, 0.0,
      0.0, 0.0, 1.0,
  };
  // ZYX Euler representation of the same rotation, in degrees. This is for
  // human-readable mounting diagnostics; the matrix above is authoritative.
  std::array<double, 3> mounting_rpy_degrees{};
};

class DualImuOffsetEstimator {
 public:
  static constexpr double kCollectionDurationSeconds = 10.0;

  void start();
  void cancel();
  bool active() const { return active_; }
  ImuOffsetEstimate add(const ImuOffsetInput& input);
  const ImuOffsetEstimate& latest() const { return latest_; }

 private:
  struct MotionPoint {
    uint64_t timestamp_us = 0;
    std::array<double, 3> gyro_dps{};
  };

  ImuOffsetEstimate status(bool force_update = false);
  ImuOffsetEstimate calculate();

  std::array<std::deque<MotionPoint>, 2> points_;
  std::array<bool, 2> sensor_seen_{};
  std::array<bool, 2> timestamp_synced_{};
  bool active_ = false;
  uint32_t samples_since_update_ = 0;
  ImuOffsetEstimate latest_;
};

}  // namespace prism_viewer
