#include "dual_imu_offset_estimator.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <vector>

namespace prism_viewer {
namespace {

constexpr uint64_t kCollectionDurationUs = 10000000;
constexpr int kMaximumOffsetUs = 20000;
constexpr int kCoarseStepUs = 100;
constexpr int kFineStepUs = 10;
constexpr uint64_t kGridStepUs = 1000;
constexpr double kMinimumMotionStddevDps = 8.0;
constexpr double kMinimumMotionPeakToPeakDps = 30.0;
constexpr double kMinimumOrientationExcitationRatio = 0.05;
constexpr double kMinimumCorrelation = 0.75;
constexpr double kMaximumCorrelationPeakWidthUs = 3000.0;
constexpr double kPi = 3.14159265358979323846;

using Vector3 = std::array<double, 3>;
using Matrix3 = std::array<double, 9>;

constexpr Matrix3 kIdentityMatrix{
    1.0, 0.0, 0.0,
    0.0, 1.0, 0.0,
    0.0, 0.0, 1.0,
};

template <typename PointDeque>
bool interpolate(const PointDeque& points, double timestamp_us, size_t* index,
                 Vector3* value) {
  if (points.size() < 2 || timestamp_us < points.front().timestamp_us ||
      timestamp_us > points.back().timestamp_us) {
    return false;
  }
  while (*index + 1 < points.size() &&
         static_cast<double>(points[*index + 1].timestamp_us) < timestamp_us) {
    ++(*index);
  }
  if (*index + 1 >= points.size()) return false;
  const auto& left = points[*index];
  const auto& right = points[*index + 1];
  const uint64_t span = right.timestamp_us - left.timestamp_us;
  if (span == 0) return false;
  const double fraction =
      (timestamp_us - static_cast<double>(left.timestamp_us)) /
      static_cast<double>(span);
  for (size_t axis = 0; axis < 3; ++axis) {
    (*value)[axis] = left.gyro_dps[axis] +
                     fraction * (right.gyro_dps[axis] - left.gyro_dps[axis]);
  }
  return true;
}

// Returns the unit eigenvector associated with the largest eigenvalue of a
// real symmetric 4x4 matrix. Jacobi rotations avoid the initialization and
// 180-degree-rotation failure modes of simple power iteration.
std::array<double, 4> largestEigenvector4(std::array<double, 16> matrix) {
  std::array<double, 16> eigenvectors{
      1.0, 0.0, 0.0, 0.0,
      0.0, 1.0, 0.0, 0.0,
      0.0, 0.0, 1.0, 0.0,
      0.0, 0.0, 0.0, 1.0,
  };

  for (int sweep = 0; sweep < 40; ++sweep) {
    int p = 0;
    int q = 1;
    double largest = 0.0;
    for (int row = 0; row < 4; ++row) {
      for (int column = row + 1; column < 4; ++column) {
        const double candidate = std::abs(matrix[row * 4 + column]);
        if (candidate > largest) {
          largest = candidate;
          p = row;
          q = column;
        }
      }
    }
    if (largest < 1e-12) break;

    const double app = matrix[p * 4 + p];
    const double aqq = matrix[q * 4 + q];
    const double apq = matrix[p * 4 + q];
    const double angle = 0.5 * std::atan2(2.0 * apq, aqq - app);
    const double c = std::cos(angle);
    const double s = std::sin(angle);

    for (int k = 0; k < 4; ++k) {
      if (k == p || k == q) continue;
      const double mkp = matrix[k * 4 + p];
      const double mkq = matrix[k * 4 + q];
      matrix[k * 4 + p] = matrix[p * 4 + k] = c * mkp - s * mkq;
      matrix[k * 4 + q] = matrix[q * 4 + k] = s * mkp + c * mkq;
    }
    matrix[p * 4 + p] = c * c * app - 2.0 * s * c * apq + s * s * aqq;
    matrix[q * 4 + q] = s * s * app + 2.0 * s * c * apq + c * c * aqq;
    matrix[p * 4 + q] = matrix[q * 4 + p] = 0.0;

    for (int row = 0; row < 4; ++row) {
      const double vrp = eigenvectors[row * 4 + p];
      const double vrq = eigenvectors[row * 4 + q];
      eigenvectors[row * 4 + p] = c * vrp - s * vrq;
      eigenvectors[row * 4 + q] = s * vrp + c * vrq;
    }
  }

  int largest_index = 0;
  for (int index = 1; index < 4; ++index) {
    if (matrix[index * 4 + index] > matrix[largest_index * 4 + largest_index]) {
      largest_index = index;
    }
  }
  std::array<double, 4> result{};
  double norm = 0.0;
  for (int row = 0; row < 4; ++row) {
    result[row] = eigenvectors[row * 4 + largest_index];
    norm += result[row] * result[row];
  }
  norm = std::sqrt(norm);
  if (norm <= std::numeric_limits<double>::epsilon()) return {1.0, 0.0, 0.0, 0.0};
  for (double& value : result) value /= norm;
  if (result[0] < 0.0) {
    for (double& value : result) value = -value;
  }
  return result;
}

Matrix3 quaternionToMatrix(const std::array<double, 4>& q) {
  const double w = q[0];
  const double x = q[1];
  const double y = q[2];
  const double z = q[3];
  return {
      1.0 - 2.0 * (y * y + z * z), 2.0 * (x * y - w * z),
      2.0 * (x * z + w * y),
      2.0 * (x * y + w * z), 1.0 - 2.0 * (x * x + z * z),
      2.0 * (y * z - w * x),
      2.0 * (x * z - w * y), 2.0 * (y * z + w * x),
      1.0 - 2.0 * (x * x + y * y),
  };
}

Vector3 multiply(const Matrix3& matrix, const Vector3& vector) {
  return {
      matrix[0] * vector[0] + matrix[1] * vector[1] + matrix[2] * vector[2],
      matrix[3] * vector[0] + matrix[4] * vector[1] + matrix[5] * vector[2],
      matrix[6] * vector[0] + matrix[7] * vector[1] + matrix[8] * vector[2],
  };
}

double dot(const Vector3& left, const Vector3& right) {
  return left[0] * right[0] + left[1] * right[1] + left[2] * right[2];
}

std::array<double, 3> eigenvaluesSymmetric3(Matrix3 matrix) {
  for (int sweep = 0; sweep < 24; ++sweep) {
    int p = 0;
    int q = 1;
    double largest = 0.0;
    for (int row = 0; row < 3; ++row) {
      for (int column = row + 1; column < 3; ++column) {
        const double candidate = std::abs(matrix[row * 3 + column]);
        if (candidate > largest) {
          largest = candidate;
          p = row;
          q = column;
        }
      }
    }
    if (largest < 1e-12) break;
    const double app = matrix[p * 3 + p];
    const double aqq = matrix[q * 3 + q];
    const double apq = matrix[p * 3 + q];
    const double angle = 0.5 * std::atan2(2.0 * apq, aqq - app);
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    for (int k = 0; k < 3; ++k) {
      if (k == p || k == q) continue;
      const double mkp = matrix[k * 3 + p];
      const double mkq = matrix[k * 3 + q];
      matrix[k * 3 + p] = matrix[p * 3 + k] = c * mkp - s * mkq;
      matrix[k * 3 + q] = matrix[q * 3 + k] = s * mkp + c * mkq;
    }
    matrix[p * 3 + p] = c * c * app - 2.0 * s * c * apq + s * s * aqq;
    matrix[q * 3 + q] = s * s * app + 2.0 * s * c * apq + c * c * aqq;
    matrix[p * 3 + q] = matrix[q * 3 + p] = 0.0;
  }
  std::array<double, 3> eigenvalues{matrix[0], matrix[4], matrix[8]};
  std::sort(eigenvalues.begin(), eigenvalues.end(), std::greater<double>());
  return eigenvalues;
}

// Davenport's q-method solves Wahba's problem without an external linear
// algebra dependency. The returned proper rotation maps IMU1 vectors to IMU0.
Matrix3 optimalRotation(const std::vector<Vector3>& imu0,
                        const std::vector<Vector3>& imu1,
                        const Vector3& mean0, const Vector3& mean1) {
  Matrix3 covariance{};  // sum((imu0-mean0) * (imu1-mean1)^T)
  for (size_t sample = 0; sample < imu0.size(); ++sample) {
    Vector3 x{};
    Vector3 y{};
    for (size_t axis = 0; axis < 3; ++axis) {
      x[axis] = imu0[sample][axis] - mean0[axis];
      y[axis] = imu1[sample][axis] - mean1[axis];
    }
    for (size_t row = 0; row < 3; ++row) {
      for (size_t column = 0; column < 3; ++column) {
        covariance[row * 3 + column] += x[row] * y[column];
      }
    }
  }

  const double sigma = covariance[0] + covariance[4] + covariance[8];
  const Vector3 z{
      covariance[7] - covariance[5],
      covariance[2] - covariance[6],
      covariance[3] - covariance[1],
  };
  std::array<double, 16> k{};
  k[0] = sigma;
  for (size_t axis = 0; axis < 3; ++axis) {
    k[axis + 1] = z[axis];
    k[(axis + 1) * 4] = z[axis];
  }
  for (size_t row = 0; row < 3; ++row) {
    for (size_t column = 0; column < 3; ++column) {
      const double symmetric = covariance[row * 3 + column] +
                               covariance[column * 3 + row];
      k[(row + 1) * 4 + column + 1] =
          symmetric - (row == column ? sigma : 0.0);
    }
  }
  return quaternionToMatrix(largestEigenvector4(k));
}

struct CorrelationResult {
  double correlation = -1.0;
  Matrix3 rotation = kIdentityMatrix;
};

template <typename PointArrays>
CorrelationResult correlationAtOffset(const PointArrays& points,
                                      uint64_t start_us, uint64_t end_us,
                                      double offset_us) {
  std::vector<Vector3> imu0;
  std::vector<Vector3> imu1;
  imu0.reserve(static_cast<size_t>((end_us - start_us) / kGridStepUs + 1));
  imu1.reserve(imu0.capacity());
  Vector3 sum0{};
  Vector3 sum1{};
  size_t index0 = 0;
  size_t index1 = 0;
  for (uint64_t timestamp_us = start_us; timestamp_us <= end_us;
       timestamp_us += kGridStepUs) {
    Vector3 x{};
    Vector3 y{};
    if (!interpolate(points[0], static_cast<double>(timestamp_us), &index0, &x) ||
        !interpolate(points[1], static_cast<double>(timestamp_us) + offset_us,
                     &index1, &y)) {
      continue;
    }
    imu0.push_back(x);
    imu1.push_back(y);
    for (size_t axis = 0; axis < 3; ++axis) {
      sum0[axis] += x[axis];
      sum1[axis] += y[axis];
    }
  }
  if (imu0.size() < 500) return {};

  Vector3 mean0{};
  Vector3 mean1{};
  for (size_t axis = 0; axis < 3; ++axis) {
    mean0[axis] = sum0[axis] / static_cast<double>(imu0.size());
    mean1[axis] = sum1[axis] / static_cast<double>(imu1.size());
  }
  CorrelationResult result;
  result.rotation = optimalRotation(imu0, imu1, mean0, mean1);

  double numerator = 0.0;
  double energy0 = 0.0;
  double energy1 = 0.0;
  for (size_t sample = 0; sample < imu0.size(); ++sample) {
    Vector3 x{};
    Vector3 y{};
    for (size_t axis = 0; axis < 3; ++axis) {
      x[axis] = imu0[sample][axis] - mean0[axis];
      y[axis] = imu1[sample][axis] - mean1[axis];
    }
    const Vector3 rotated_y = multiply(result.rotation, y);
    numerator += dot(x, rotated_y);
    energy0 += dot(x, x);
    energy1 += dot(y, y);
  }
  const double denominator = std::sqrt(energy0 * energy1);
  if (denominator <= std::numeric_limits<double>::epsilon()) return {};
  result.correlation = numerator / denominator;
  return result;
}

struct SearchResult {
  int best_offset_us = 0;
  double best_correlation = -1.0;
  Matrix3 best_rotation = kIdentityMatrix;
  std::vector<double> correlations;
};

template <typename PointArrays>
SearchResult searchOffsets(const PointArrays& points, uint64_t start_us,
                           uint64_t end_us, int minimum_offset_us,
                           int maximum_offset_us, int step_us) {
  SearchResult result;
  result.correlations.reserve(
      static_cast<size_t>((maximum_offset_us - minimum_offset_us) / step_us + 1));
  for (int offset_us = minimum_offset_us; offset_us <= maximum_offset_us;
       offset_us += step_us) {
    const CorrelationResult candidate =
        correlationAtOffset(points, start_us, end_us, offset_us);
    result.correlations.push_back(candidate.correlation);
    if (candidate.correlation > result.best_correlation) {
      result.best_correlation = candidate.correlation;
      result.best_offset_us = offset_us;
      result.best_rotation = candidate.rotation;
    }
  }
  return result;
}

std::array<double, 3> rotationToRpyDegrees(const Matrix3& rotation) {
  const double pitch = std::asin(std::clamp(-rotation[6], -1.0, 1.0));
  const double cosine_pitch = std::cos(pitch);
  double roll = 0.0;
  double yaw = 0.0;
  if (std::abs(cosine_pitch) > 1e-8) {
    roll = std::atan2(rotation[7], rotation[8]);
    yaw = std::atan2(rotation[3], rotation[0]);
  } else {
    roll = std::atan2(-rotation[5], rotation[4]);
  }
  constexpr double kRadiansToDegrees = 180.0 / kPi;
  return {roll * kRadiansToDegrees, pitch * kRadiansToDegrees,
          yaw * kRadiansToDegrees};
}

}  // namespace

void DualImuOffsetEstimator::start() {
  for (auto& points : points_) points.clear();
  sensor_seen_.fill(false);
  timestamp_synced_.fill(false);
  samples_since_update_ = 0;
  active_ = true;
  latest_ = {};
  latest_.updated = true;
  latest_.state = ImuOffsetState::WaitingForBothImus;
}

void DualImuOffsetEstimator::cancel() {
  active_ = false;
  latest_.updated = true;
  latest_.state = ImuOffsetState::Idle;
}

ImuOffsetEstimate DualImuOffsetEstimator::add(const ImuOffsetInput& input) {
  ImuOffsetEstimate unchanged = latest_;
  unchanged.updated = false;
  if (!active_ || input.sensor_id >= points_.size()) return unchanged;

  const size_t sensor = input.sensor_id;
  sensor_seen_[sensor] = true;
  timestamp_synced_[sensor] = input.timestamp_synced;
  if (!input.timestamp_synced) {
    points_[sensor].clear();
  } else {
    auto& points = points_[sensor];
    if (!points.empty() && input.timestamp_us <= points.back().timestamp_us) {
      points.clear();
    }
    Vector3 gyro{};
    for (size_t axis = 0; axis < 3; ++axis) {
      gyro[axis] = static_cast<double>(input.gyro_mdps[axis]) / 1000.0;
    }
    points.push_back({input.timestamp_us, gyro});
  }

  ++samples_since_update_;
  if (sensor_seen_[0] && sensor_seen_[1] && timestamp_synced_[0] &&
      timestamp_synced_[1] && !points_[0].empty() && !points_[1].empty()) {
    const uint64_t common_start =
        std::max(points_[0].front().timestamp_us, points_[1].front().timestamp_us);
    const uint64_t common_end =
        std::min(points_[0].back().timestamp_us, points_[1].back().timestamp_us);
    if (common_end > common_start &&
        common_end - common_start >= kCollectionDurationUs) {
      active_ = false;
      return calculate();
    }
  }

  if (samples_since_update_ < 200) return unchanged;
  samples_since_update_ = 0;
  return status(true);
}

ImuOffsetEstimate DualImuOffsetEstimator::status(bool force_update) {
  ImuOffsetEstimate result;
  result.updated = force_update;
  result.sample_count[0] = points_[0].size();
  result.sample_count[1] = points_[1].size();
  if (!sensor_seen_[0] || !sensor_seen_[1]) {
    result.state = ImuOffsetState::WaitingForBothImus;
  } else if (!timestamp_synced_[0] || !timestamp_synced_[1]) {
    result.state = ImuOffsetState::WaitingForTimeSync;
  } else if (!points_[0].empty() && !points_[1].empty()) {
    const uint64_t common_start =
        std::max(points_[0].front().timestamp_us, points_[1].front().timestamp_us);
    const uint64_t common_end =
        std::min(points_[0].back().timestamp_us, points_[1].back().timestamp_us);
    if (common_end > common_start) {
      result.duration_s = static_cast<double>(common_end - common_start) / 1e6;
    }
    result.progress = std::clamp(
        result.duration_s / kCollectionDurationSeconds, 0.0, 1.0);
    result.state = ImuOffsetState::Collecting;
  } else {
    result.state = ImuOffsetState::WaitingForBothImus;
  }
  latest_ = result;
  return result;
}

ImuOffsetEstimate DualImuOffsetEstimator::calculate() {
  ImuOffsetEstimate result;
  result.updated = true;
  result.duration_s = kCollectionDurationSeconds;
  result.progress = 1.0;
  result.sample_count[0] = points_[0].size();
  result.sample_count[1] = points_[1].size();

  const uint64_t start_us =
      std::max(points_[0].front().timestamp_us,
               points_[1].front().timestamp_us + kMaximumOffsetUs);
  const uint64_t latest_common =
      std::min(points_[0].back().timestamp_us, points_[1].back().timestamp_us);
  if (latest_common <= kMaximumOffsetUs) {
    result.state = ImuOffsetState::LowConfidence;
    latest_ = result;
    return result;
  }
  const uint64_t end_us = latest_common - kMaximumOffsetUs;
  if (end_us <= start_us || end_us - start_us < 5000000) {
    result.state = ImuOffsetState::LowConfidence;
    latest_ = result;
    return result;
  }

  double sum = 0.0;
  double sum_squares = 0.0;
  double minimum = std::numeric_limits<double>::max();
  double maximum = std::numeric_limits<double>::lowest();
  Vector3 gyro_sum{};
  Matrix3 gyro_outer_sum{};
  size_t count = 0;
  size_t index = 0;
  for (uint64_t timestamp_us = start_us; timestamp_us <= end_us;
       timestamp_us += kGridStepUs) {
    Vector3 value{};
    if (!interpolate(points_[0], static_cast<double>(timestamp_us), &index, &value)) {
      continue;
    }
    const double norm = std::sqrt(dot(value, value));
    sum += norm;
    sum_squares += norm * norm;
    minimum = std::min(minimum, norm);
    maximum = std::max(maximum, norm);
    for (size_t row = 0; row < 3; ++row) {
      gyro_sum[row] += value[row];
      for (size_t column = 0; column < 3; ++column) {
        gyro_outer_sum[row * 3 + column] += value[row] * value[column];
      }
    }
    ++count;
  }
  if (count > 1) {
    const double mean = sum / static_cast<double>(count);
    result.motion_stddev_dps = std::sqrt(std::max(
        0.0, sum_squares / static_cast<double>(count) - mean * mean));
    result.motion_peak_to_peak_dps = maximum - minimum;
    Matrix3 gyro_covariance{};
    for (size_t row = 0; row < 3; ++row) {
      for (size_t column = 0; column < 3; ++column) {
        gyro_covariance[row * 3 + column] =
            gyro_outer_sum[row * 3 + column] / static_cast<double>(count) -
            (gyro_sum[row] / static_cast<double>(count)) *
                (gyro_sum[column] / static_cast<double>(count));
      }
    }
    const auto eigenvalues = eigenvaluesSymmetric3(gyro_covariance);
    if (eigenvalues[0] > std::numeric_limits<double>::epsilon()) {
      result.orientation_excitation_ratio =
          std::max(0.0, eigenvalues[1]) / eigenvalues[0];
    }
  }
  if (result.motion_stddev_dps < kMinimumMotionStddevDps ||
      result.motion_peak_to_peak_dps < kMinimumMotionPeakToPeakDps ||
      result.orientation_excitation_ratio < kMinimumOrientationExcitationRatio) {
    result.state = ImuOffsetState::InsufficientMotion;
    latest_ = result;
    return result;
  }

  const SearchResult coarse = searchOffsets(
      points_, start_us, end_us, -kMaximumOffsetUs, kMaximumOffsetUs,
      kCoarseStepUs);
  const int fine_minimum =
      std::max(-kMaximumOffsetUs, coarse.best_offset_us - kCoarseStepUs);
  const int fine_maximum =
      std::min(kMaximumOffsetUs, coarse.best_offset_us + kCoarseStepUs);
  const SearchResult fine = searchOffsets(points_, start_us, end_us, fine_minimum,
                                          fine_maximum, kFineStepUs);
  result.correlation = fine.best_correlation;
  result.offset_us = fine.best_offset_us;
  result.imu1_to_imu0_rotation = fine.best_rotation;
  result.mounting_rpy_degrees = rotationToRpyDegrees(fine.best_rotation);

  const size_t fine_index = static_cast<size_t>(
      (fine.best_offset_us - fine_minimum) / kFineStepUs);
  if (fine_index > 0 && fine_index + 1 < fine.correlations.size()) {
    const double left = fine.correlations[fine_index - 1];
    const double middle = fine.correlations[fine_index];
    const double right = fine.correlations[fine_index + 1];
    const double denominator = left - 2.0 * middle + right;
    if (std::abs(denominator) > 1e-12) {
      const double fraction = std::clamp(
          0.5 * (left - right) / denominator, -1.0, 1.0);
      result.offset_us += fraction * kFineStepUs;
    }
  }

  const size_t coarse_index = static_cast<size_t>(
      (coarse.best_offset_us + kMaximumOffsetUs) / kCoarseStepUs);
  // Measure the near-top width, not the entire high-correlation shoulder. A
  // 0.001 drop is sensitive to timing ambiguity while still tolerating noise.
  const double peak_floor = coarse.best_correlation - 0.001;
  size_t peak_left = coarse_index;
  size_t peak_right = coarse_index;
  while (peak_left > 0 && coarse.correlations[peak_left - 1] >= peak_floor) {
    --peak_left;
  }
  while (peak_right + 1 < coarse.correlations.size() &&
         coarse.correlations[peak_right + 1] >= peak_floor) {
    ++peak_right;
  }
  result.correlation_peak_width_us =
      static_cast<double>(peak_right - peak_left) * kCoarseStepUs;

  if (coarse.best_offset_us == -kMaximumOffsetUs ||
      coarse.best_offset_us == kMaximumOffsetUs ||
      result.correlation < kMinimumCorrelation ||
      result.correlation_peak_width_us > kMaximumCorrelationPeakWidthUs) {
    result.state = ImuOffsetState::LowConfidence;
  } else {
    result.valid = true;
    result.state = ImuOffsetState::Complete;
  }
  latest_ = result;
  return result;
}

}  // namespace prism_viewer
