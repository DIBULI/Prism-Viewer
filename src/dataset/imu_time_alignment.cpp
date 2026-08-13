#include "dataset/imu_time_alignment.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>
#include <string_view>
#include <vector>

namespace prism_viewer::dataset {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kRadiansToDegrees = 180.0 / kPi;
constexpr uint64_t kAnalysisWindowUs = 12000000;
constexpr uint64_t kGridStepUs = 1000;
constexpr size_t kHighPassLagSamples = 20;
constexpr int kMaximumOffsetUs = 50000;
constexpr int kCoarseStepUs = 100;
constexpr int kFineStepUs = 10;
constexpr double kMinimumMotionStddevDps = 3.0;
constexpr double kMinimumMotionPeakToPeakDps = 15.0;
constexpr double kMinimumCorrelation = 0.75;
constexpr double kMaximumPeakWidthUs = 5000.0;
constexpr uint64_t kPlausibleRkRealtimeUs = 100000000000000ULL;

using Vector3 = std::array<double, 3>;
using Matrix3 = std::array<double, 9>;

struct ImuPoint {
  uint64_t timestamp_us = 0;
  Vector3 gyro_dps{};
};

struct FileScan {
  bool ok = false;
  bool cancelled = false;
  uint64_t rows = 0;
  uint64_t first_us = 0;
  uint64_t last_us = 0;
  std::string error;
};

bool parseUnsigned(std::string_view token, uint64_t* value) {
  if (token.empty()) return false;
  uint64_t parsed = 0;
  const auto result =
      std::from_chars(token.data(), token.data() + token.size(), parsed);
  if (result.ec != std::errc{} || result.ptr != token.data() + token.size()) {
    return false;
  }
  *value = parsed;
  return true;
}

bool parseTimestampUs(const std::string& token, uint64_t* timestamp_us) {
  const size_t decimal = token.find('.');
  const size_t seconds_length =
      decimal == std::string::npos ? token.size() : decimal;
  const std::string_view seconds_text(token.data(), seconds_length);
  uint64_t seconds = 0;
  if (!parseUnsigned(seconds_text, &seconds)) return false;
  uint64_t microseconds = 0;
  if (decimal != std::string::npos) {
    std::string_view fraction(token.data() + decimal + 1,
                              token.size() - decimal - 1);
    if (fraction.empty() || fraction.size() > 9u) return false;
    const size_t used = std::min<size_t>(fraction.size(), 6u);
    if (!parseUnsigned(fraction.substr(0, used), &microseconds)) return false;
    for (size_t digit = used; digit < 6u; ++digit) microseconds *= 10u;
    for (size_t digit = 6u; digit < fraction.size(); ++digit) {
      if (fraction[digit] != '0') return false;
    }
  }
  if (seconds > (std::numeric_limits<uint64_t>::max() - microseconds) /
                    1000000ULL) {
    return false;
  }
  *timestamp_us = seconds * 1000000ULL + microseconds;
  return true;
}

bool parseImuLine(const std::string& line, uint64_t* timestamp_us,
                  Vector3* gyro_dps) {
  std::istringstream input(line);
  input.imbue(std::locale::classic());
  std::string timestamp;
  std::array<double, 3> acceleration{};
  Vector3 gyro_rad_s{};
  if (!(input >> timestamp >> acceleration[0] >> acceleration[1] >>
        acceleration[2] >> gyro_rad_s[0] >> gyro_rad_s[1] >>
        gyro_rad_s[2]) ||
      !parseTimestampUs(timestamp, timestamp_us)) {
    return false;
  }
  for (double value : acceleration) {
    if (!std::isfinite(value)) return false;
  }
  for (size_t axis = 0; axis < gyro_rad_s.size(); ++axis) {
    if (!std::isfinite(gyro_rad_s[axis])) return false;
    (*gyro_dps)[axis] = gyro_rad_s[axis] * kRadiansToDegrees;
  }
  return true;
}

bool skipLine(const std::string& line) {
  const size_t first = line.find_first_not_of(" \t\r");
  return first == std::string::npos || line[first] == '#';
}

FileScan scanFile(const std::filesystem::path& path,
                  uint64_t* checked_rows,
                  const ImuTimeAlignmentProgressCallback& progress,
                  const ImuTimeAlignmentCancelCallback& cancelled) {
  FileScan result;
  std::ifstream input(path);
  if (!input.is_open()) {
    result.error = "cannot open " + path.filename().string();
    return result;
  }
  std::string line;
  uint64_t line_number = 0;
  while (std::getline(input, line)) {
    ++line_number;
    if (skipLine(line)) continue;
    uint64_t timestamp_us = 0;
    Vector3 gyro{};
    if (!parseImuLine(line, &timestamp_us, &gyro)) {
      result.error = path.filename().string() + ":" +
                     std::to_string(line_number) + " is not a valid IMU row";
      return result;
    }
    if (timestamp_us < kPlausibleRkRealtimeUs) {
      result.error = path.filename().string() + ":" +
                     std::to_string(line_number) +
                     " is not in the RK CLOCK_REALTIME epoch";
      return result;
    }
    if (result.rows != 0u && timestamp_us <= result.last_us) {
      result.error = path.filename().string() + ":" +
                     std::to_string(line_number) +
                     " has a repeated or backwards timestamp";
      return result;
    }
    if (result.rows == 0u) result.first_us = timestamp_us;
    result.last_us = timestamp_us;
    ++result.rows;
    ++*checked_rows;
    if (((*checked_rows) & 4095u) == 0u) {
      if (progress) progress(*checked_rows, path.filename().string());
      if (cancelled && cancelled()) {
        result.cancelled = true;
        return result;
      }
    }
  }
  if (!input.eof()) {
    result.error = "failed while reading " + path.filename().string();
    return result;
  }
  result.ok = result.rows != 0u;
  if (!result.ok) result.error = path.filename().string() + " has no samples";
  return result;
}

bool loadRange(const std::filesystem::path& path, uint64_t start_us,
               uint64_t end_us, std::vector<ImuPoint>* points,
               uint64_t* checked_rows,
               const ImuTimeAlignmentProgressCallback& progress,
               const ImuTimeAlignmentCancelCallback& cancelled,
               std::string* error, bool* was_cancelled) {
  std::ifstream input(path);
  if (!input.is_open()) {
    *error = "cannot open " + path.filename().string();
    return false;
  }
  std::string line;
  uint64_t line_number = 0;
  while (std::getline(input, line)) {
    ++line_number;
    if (skipLine(line)) continue;
    uint64_t timestamp_us = 0;
    Vector3 gyro{};
    if (!parseImuLine(line, &timestamp_us, &gyro)) {
      *error = path.filename().string() + ":" +
               std::to_string(line_number) + " is not a valid IMU row";
      return false;
    }
    if (timestamp_us > end_us) break;
    if (timestamp_us >= start_us) points->push_back({timestamp_us, gyro});
    ++*checked_rows;
    if (((*checked_rows) & 4095u) == 0u) {
      if (progress) progress(*checked_rows, path.filename().string());
      if (cancelled && cancelled()) {
        *was_cancelled = true;
        return false;
      }
    }
  }
  if (!input.eof() && input.fail()) {
    *error = "failed while reading " + path.filename().string();
    return false;
  }
  return true;
}

template <typename Points>
bool interpolate(const Points& points, double timestamp_us, size_t* index,
                 Vector3* value) {
  if (points.size() < 2u || timestamp_us < points.front().timestamp_us ||
      timestamp_us > points.back().timestamp_us) {
    return false;
  }
  while (*index + 1u < points.size() &&
         static_cast<double>(points[*index + 1u].timestamp_us) < timestamp_us) {
    ++*index;
  }
  if (*index + 1u >= points.size()) return false;
  const auto& left = points[*index];
  const auto& right = points[*index + 1u];
  const uint64_t span = right.timestamp_us - left.timestamp_us;
  if (span == 0u) return false;
  const double fraction =
      (timestamp_us - static_cast<double>(left.timestamp_us)) /
      static_cast<double>(span);
  for (size_t axis = 0; axis < 3u; ++axis) {
    (*value)[axis] = left.gyro_dps[axis] +
                     fraction * (right.gyro_dps[axis] - left.gyro_dps[axis]);
  }
  return true;
}

double dot(const Vector3& left, const Vector3& right) {
  return left[0] * right[0] + left[1] * right[1] + left[2] * right[2];
}

Vector3 multiply(const Matrix3& matrix, const Vector3& vector) {
  return {
      matrix[0] * vector[0] + matrix[1] * vector[1] + matrix[2] * vector[2],
      matrix[3] * vector[0] + matrix[4] * vector[1] + matrix[5] * vector[2],
      matrix[6] * vector[0] + matrix[7] * vector[1] + matrix[8] * vector[2],
  };
}

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
    if (matrix[index * 4 + index] >
        matrix[largest_index * 4 + largest_index]) {
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
  if (norm <= std::numeric_limits<double>::epsilon()) {
    return {1.0, 0.0, 0.0, 0.0};
  }
  for (double& value : result) value /= norm;
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

Matrix3 optimalRotation(const std::vector<Vector3>& imu0,
                        const std::vector<Vector3>& lidar,
                        const Vector3& mean0, const Vector3& mean_lidar) {
  Matrix3 covariance{};
  for (size_t sample = 0; sample < imu0.size(); ++sample) {
    Vector3 x{};
    Vector3 y{};
    for (size_t axis = 0; axis < 3u; ++axis) {
      x[axis] = imu0[sample][axis] - mean0[axis];
      y[axis] = lidar[sample][axis] - mean_lidar[axis];
    }
    for (size_t row = 0; row < 3u; ++row) {
      for (size_t column = 0; column < 3u; ++column) {
        covariance[row * 3u + column] += x[row] * y[column];
      }
    }
  }
  const double sigma = covariance[0] + covariance[4] + covariance[8];
  const Vector3 z{covariance[7] - covariance[5],
                  covariance[2] - covariance[6],
                  covariance[3] - covariance[1]};
  std::array<double, 16> k{};
  k[0] = sigma;
  for (size_t axis = 0; axis < 3u; ++axis) {
    k[axis + 1u] = z[axis];
    k[(axis + 1u) * 4u] = z[axis];
  }
  for (size_t row = 0; row < 3u; ++row) {
    for (size_t column = 0; column < 3u; ++column) {
      const double symmetric = covariance[row * 3u + column] +
                               covariance[column * 3u + row];
      k[(row + 1u) * 4u + column + 1u] =
          symmetric - (row == column ? sigma : 0.0);
    }
  }
  return quaternionToMatrix(largestEigenvector4(k));
}

struct CorrelationResult {
  double correlation = -1.0;
};

CorrelationResult correlationAtOffset(
    const std::array<std::vector<ImuPoint>, 2>& points, uint64_t start_us,
    uint64_t end_us, double offset_us) {
  std::vector<Vector3> imu0;
  std::vector<Vector3> lidar;
  imu0.reserve(static_cast<size_t>((end_us - start_us) / kGridStepUs + 1u));
  lidar.reserve(imu0.capacity());
  Vector3 sum0{};
  Vector3 sum_lidar{};
  size_t index0 = 0;
  size_t index_lidar = 0;
  for (uint64_t timestamp_us = start_us; timestamp_us <= end_us;
       timestamp_us += kGridStepUs) {
    Vector3 x{};
    Vector3 y{};
    if (!interpolate(points[0], static_cast<double>(timestamp_us), &index0,
                     &x) ||
        !interpolate(points[1], static_cast<double>(timestamp_us) + offset_us,
                     &index_lidar, &y)) {
      continue;
    }
    imu0.push_back(x);
    lidar.push_back(y);
    for (size_t axis = 0; axis < 3u; ++axis) {
      sum0[axis] += x[axis];
      sum_lidar[axis] += y[axis];
    }
  }
  if (imu0.size() < 3000u) return {};
  // A DC/slow-motion dominated gyro trace produces a very broad correlation
  // peak and can make millisecond timing look more precise than it is. Use a
  // symmetric-time high-pass equivalent (x[t] - x[t-20 ms]) for both sensors.
  // The same filter is applied on the common grid, so it adds no relative
  // phase shift while retaining motion edges that identify the offset.
  if (imu0.size() <= kHighPassLagSamples) return {};
  std::vector<Vector3> filtered_imu0;
  std::vector<Vector3> filtered_lidar;
  filtered_imu0.reserve(imu0.size() - kHighPassLagSamples);
  filtered_lidar.reserve(lidar.size() - kHighPassLagSamples);
  for (size_t sample = kHighPassLagSamples; sample < imu0.size(); ++sample) {
    Vector3 x{};
    Vector3 y{};
    for (size_t axis = 0; axis < 3u; ++axis) {
      x[axis] = imu0[sample][axis] -
                imu0[sample - kHighPassLagSamples][axis];
      y[axis] = lidar[sample][axis] -
                lidar[sample - kHighPassLagSamples][axis];
    }
    filtered_imu0.push_back(x);
    filtered_lidar.push_back(y);
  }
  imu0 = std::move(filtered_imu0);
  lidar = std::move(filtered_lidar);
  sum0 = {};
  sum_lidar = {};
  for (size_t sample = 0; sample < imu0.size(); ++sample) {
    for (size_t axis = 0; axis < 3u; ++axis) {
      sum0[axis] += imu0[sample][axis];
      sum_lidar[axis] += lidar[sample][axis];
    }
  }
  Vector3 mean0{};
  Vector3 mean_lidar{};
  for (size_t axis = 0; axis < 3u; ++axis) {
    mean0[axis] = sum0[axis] / static_cast<double>(imu0.size());
    mean_lidar[axis] = sum_lidar[axis] / static_cast<double>(lidar.size());
  }
  const Matrix3 rotation = optimalRotation(imu0, lidar, mean0, mean_lidar);
  double numerator = 0.0;
  double energy0 = 0.0;
  double energy_lidar = 0.0;
  for (size_t sample = 0; sample < imu0.size(); ++sample) {
    Vector3 x{};
    Vector3 y{};
    for (size_t axis = 0; axis < 3u; ++axis) {
      x[axis] = imu0[sample][axis] - mean0[axis];
      y[axis] = lidar[sample][axis] - mean_lidar[axis];
    }
    const Vector3 rotated = multiply(rotation, y);
    numerator += dot(x, rotated);
    energy0 += dot(x, x);
    energy_lidar += dot(y, y);
  }
  const double denominator = std::sqrt(energy0 * energy_lidar);
  if (denominator <= std::numeric_limits<double>::epsilon()) return {};
  return {numerator / denominator};
}

struct SearchResult {
  int best_offset_us = 0;
  double best_correlation = -1.0;
  std::vector<double> correlations;
  bool cancelled = false;
};

SearchResult searchOffsets(const std::array<std::vector<ImuPoint>, 2>& points,
                           uint64_t start_us, uint64_t end_us,
                           int minimum_offset_us, int maximum_offset_us,
                           int step_us, uint64_t* checked_rows,
                           const ImuTimeAlignmentProgressCallback& progress,
                           const ImuTimeAlignmentCancelCallback& cancelled) {
  SearchResult result;
  result.correlations.reserve(static_cast<size_t>(
      (maximum_offset_us - minimum_offset_us) / step_us + 1));
  for (int offset_us = minimum_offset_us; offset_us <= maximum_offset_us;
       offset_us += step_us) {
    const double correlation =
        correlationAtOffset(points, start_us, end_us, offset_us).correlation;
    result.correlations.push_back(correlation);
    if (correlation > result.best_correlation) {
      result.best_correlation = correlation;
      result.best_offset_us = offset_us;
    }
    ++*checked_rows;
    if (((*checked_rows) & 15u) == 0u) {
      if (progress) progress(*checked_rows, "gyroscope correlation search");
      if (cancelled && cancelled()) {
        result.cancelled = true;
        return result;
      }
    }
  }
  return result;
}

std::pair<double, double> motionStatistics(
    const std::vector<ImuPoint>& points, uint64_t start_us, uint64_t end_us) {
  double sum = 0.0;
  double sum_squares = 0.0;
  double minimum = std::numeric_limits<double>::max();
  double maximum = std::numeric_limits<double>::lowest();
  size_t count = 0;
  for (const auto& point : points) {
    if (point.timestamp_us < start_us) continue;
    if (point.timestamp_us > end_us) break;
    const double norm = std::sqrt(dot(point.gyro_dps, point.gyro_dps));
    sum += norm;
    sum_squares += norm * norm;
    minimum = std::min(minimum, norm);
    maximum = std::max(maximum, norm);
    ++count;
  }
  if (count < 2u) return {0.0, 0.0};
  const double mean = sum / static_cast<double>(count);
  const double standard_deviation = std::sqrt(std::max(
      0.0, sum_squares / static_cast<double>(count) - mean * mean));
  return {standard_deviation, maximum - minimum};
}

struct MotionBin {
  double sum = 0.0;
  double sum_squares = 0.0;
  double minimum = std::numeric_limits<double>::max();
  double maximum = std::numeric_limits<double>::lowest();
  uint64_t count = 0;
};

bool findMotionWindow(const std::filesystem::path& path, uint64_t start_us,
                      uint64_t end_us, uint64_t* selected_start_us,
                      uint64_t* checked_rows,
                      const ImuTimeAlignmentProgressCallback& progress,
                      const ImuTimeAlignmentCancelCallback& cancelled,
                      std::string* error, bool* was_cancelled) {
  if (end_us - start_us <= kAnalysisWindowUs) {
    *selected_start_us = start_us;
    return true;
  }
  constexpr uint64_t kBinUs = 1000000ULL;
  const size_t bin_count =
      static_cast<size_t>((end_us - start_us) / kBinUs + 1u);
  std::vector<MotionBin> bins(bin_count);
  std::ifstream input(path);
  if (!input.is_open()) {
    *error = "cannot open " + path.filename().string();
    return false;
  }
  std::string line;
  uint64_t line_number = 0;
  while (std::getline(input, line)) {
    ++line_number;
    if (skipLine(line)) continue;
    uint64_t timestamp_us = 0;
    Vector3 gyro{};
    if (!parseImuLine(line, &timestamp_us, &gyro)) {
      *error = path.filename().string() + ":" +
               std::to_string(line_number) + " is not a valid IMU row";
      return false;
    }
    if (timestamp_us > end_us) break;
    if (timestamp_us >= start_us) {
      const size_t bin = static_cast<size_t>((timestamp_us - start_us) /
                                             kBinUs);
      if (bin < bins.size()) {
        const double norm = std::sqrt(dot(gyro, gyro));
        auto& statistics = bins[bin];
        statistics.sum += norm;
        statistics.sum_squares += norm * norm;
        statistics.minimum = std::min(statistics.minimum, norm);
        statistics.maximum = std::max(statistics.maximum, norm);
        ++statistics.count;
      }
    }
    ++*checked_rows;
    if (((*checked_rows) & 4095u) == 0u) {
      if (progress) progress(*checked_rows, path.filename().string());
      if (cancelled && cancelled()) {
        *was_cancelled = true;
        return false;
      }
    }
  }
  if (!input.eof() && input.fail()) {
    *error = "failed while reading " + path.filename().string();
    return false;
  }

  constexpr size_t kWindowBins =
      static_cast<size_t>(kAnalysisWindowUs / kBinUs);
  uint64_t best_start = start_us;
  double best_score = -1.0;
  for (size_t first = 0; first + kWindowBins <= bins.size(); ++first) {
    double sum = 0.0;
    double sum_squares = 0.0;
    double minimum = std::numeric_limits<double>::max();
    double maximum = std::numeric_limits<double>::lowest();
    uint64_t count = 0;
    for (size_t bin = first; bin < first + kWindowBins; ++bin) {
      const MotionBin& statistics = bins[bin];
      if (statistics.count == 0u) continue;
      sum += statistics.sum;
      sum_squares += statistics.sum_squares;
      minimum = std::min(minimum, statistics.minimum);
      maximum = std::max(maximum, statistics.maximum);
      count += statistics.count;
    }
    if (count < 2u) continue;
    const double mean = sum / static_cast<double>(count);
    const double standard_deviation = std::sqrt(std::max(
        0.0, sum_squares / static_cast<double>(count) - mean * mean));
    const double score = standard_deviation + 0.1 * (maximum - minimum);
    if (score > best_score) {
      best_score = score;
      best_start = start_us + static_cast<uint64_t>(first) * kBinUs;
    }
  }
  *selected_start_us = best_start;
  return true;
}

}  // namespace

ImuTimeAlignmentResult analyzeImu0LidarImuTimeOffset(
    const std::filesystem::path& dataset_root,
    const ImuTimeAlignmentProgressCallback& progress,
    const ImuTimeAlignmentCancelCallback& cancelled) {
  ImuTimeAlignmentResult result;
  const auto imu0_path = dataset_root / "imu0.tum";
  const auto lidar_path = dataset_root / "lidar_imu.tum";
  if (!std::filesystem::is_regular_file(imu0_path) ||
      !std::filesystem::is_regular_file(lidar_path)) {
    result.status = ImuTimeAlignmentStatus::MissingInput;
    result.message = "imu0.tum and lidar_imu.tum are both required";
    return result;
  }

  uint64_t checked_rows = 0;
  const FileScan imu0 =
      scanFile(imu0_path, &checked_rows, progress, cancelled);
  if (imu0.cancelled) {
    result.cancelled = true;
    result.status = ImuTimeAlignmentStatus::Cancelled;
    return result;
  }
  if (!imu0.ok) {
    result.status = ImuTimeAlignmentStatus::InvalidData;
    result.message = imu0.error;
    return result;
  }
  const FileScan lidar =
      scanFile(lidar_path, &checked_rows, progress, cancelled);
  if (lidar.cancelled) {
    result.cancelled = true;
    result.status = ImuTimeAlignmentStatus::Cancelled;
    return result;
  }
  if (!lidar.ok) {
    result.status = ImuTimeAlignmentStatus::InvalidData;
    result.message = lidar.error;
    return result;
  }
  result.imu0_rows = imu0.rows;
  result.lidar_imu_rows = lidar.rows;
  result.common_start_us = std::max(imu0.first_us, lidar.first_us);
  result.common_end_us = std::min(imu0.last_us, lidar.last_us);
  if (result.common_end_us <= result.common_start_us + 5000000ULL) {
    result.status = ImuTimeAlignmentStatus::InsufficientOverlap;
    result.message = "IMU0 and LiDAR IMU have less than five seconds of "
                     "overlapping RK time";
    return result;
  }

  uint64_t candidate_start = result.common_start_us;
  bool was_cancelled = false;
  std::string error;
  if (!findMotionWindow(imu0_path, result.common_start_us,
                        result.common_end_us, &candidate_start,
                        &checked_rows, progress, cancelled, &error,
                        &was_cancelled)) {
    result.cancelled = was_cancelled;
    result.status = was_cancelled ? ImuTimeAlignmentStatus::Cancelled
                                  : ImuTimeAlignmentStatus::InvalidData;
    result.message = error;
    return result;
  }
  const uint64_t candidate_end =
      std::min(result.common_end_us, candidate_start + kAnalysisWindowUs);
  const uint64_t load_start =
      candidate_start > static_cast<uint64_t>(kMaximumOffsetUs + kGridStepUs)
          ? candidate_start - kMaximumOffsetUs - kGridStepUs
          : candidate_start;
  const uint64_t load_end = std::min(
      result.common_end_us,
      candidate_end + kMaximumOffsetUs + kGridStepUs);
  std::array<std::vector<ImuPoint>, 2> points;
  if (!loadRange(imu0_path, load_start, load_end, &points[0],
                 &checked_rows, progress, cancelled, &error,
                 &was_cancelled) ||
      !loadRange(lidar_path, load_start, load_end, &points[1],
                 &checked_rows, progress, cancelled, &error,
                 &was_cancelled)) {
    result.cancelled = was_cancelled;
    result.status = was_cancelled ? ImuTimeAlignmentStatus::Cancelled
                                  : ImuTimeAlignmentStatus::InvalidData;
    result.message = error;
    return result;
  }
  result.analyzed_imu0_samples = points[0].size();
  result.analyzed_lidar_imu_samples = points[1].size();
  if (points[0].size() < 1000u || points[1].size() < 500u) {
    result.status = ImuTimeAlignmentStatus::InsufficientOverlap;
    result.message = "not enough overlapping IMU samples";
    return result;
  }

  result.analyzed_duration_s =
      static_cast<double>(candidate_end - candidate_start) / 1e6;
  const auto motion =
      motionStatistics(points[0], candidate_start, candidate_end);
  result.motion_stddev_dps = motion.first;
  result.motion_peak_to_peak_dps = motion.second;
  if (motion.first < kMinimumMotionStddevDps ||
      motion.second < kMinimumMotionPeakToPeakDps) {
    result.status = ImuTimeAlignmentStatus::InsufficientMotion;
    result.message = "the selected interval does not contain enough angular "
                     "motion to estimate a time offset";
    return result;
  }

  const uint64_t search_start = candidate_start + kMaximumOffsetUs;
  const uint64_t search_end = candidate_end - kMaximumOffsetUs;
  if (search_end <= search_start + 3000000ULL) {
    result.status = ImuTimeAlignmentStatus::InsufficientOverlap;
    result.message = "the common analysis window is too short";
    return result;
  }
  const SearchResult coarse = searchOffsets(
      points, search_start, search_end, -kMaximumOffsetUs,
      kMaximumOffsetUs, kCoarseStepUs, &checked_rows, progress, cancelled);
  if (coarse.cancelled) {
    result.cancelled = true;
    result.status = ImuTimeAlignmentStatus::Cancelled;
    return result;
  }
  const int fine_minimum =
      std::max(-kMaximumOffsetUs, coarse.best_offset_us - kCoarseStepUs);
  const int fine_maximum =
      std::min(kMaximumOffsetUs, coarse.best_offset_us + kCoarseStepUs);
  const SearchResult fine = searchOffsets(
      points, search_start, search_end, fine_minimum, fine_maximum,
      kFineStepUs, &checked_rows, progress, cancelled);
  if (fine.cancelled) {
    result.cancelled = true;
    result.status = ImuTimeAlignmentStatus::Cancelled;
    return result;
  }
  result.offset_us = fine.best_offset_us;
  result.correlation = fine.best_correlation;

  const size_t fine_index = static_cast<size_t>(
      (fine.best_offset_us - fine_minimum) / kFineStepUs);
  if (fine_index > 0u && fine_index + 1u < fine.correlations.size()) {
    const double left = fine.correlations[fine_index - 1u];
    const double middle = fine.correlations[fine_index];
    const double right = fine.correlations[fine_index + 1u];
    const double denominator = left - 2.0 * middle + right;
    if (std::abs(denominator) > 1e-12) {
      result.offset_us += std::clamp(
          0.5 * (left - right) / denominator, -1.0, 1.0) * kFineStepUs;
    }
  }

  const size_t coarse_index = static_cast<size_t>(
      (coarse.best_offset_us + kMaximumOffsetUs) / kCoarseStepUs);
  const double peak_floor = coarse.best_correlation - 0.001;
  size_t peak_left = coarse_index;
  size_t peak_right = coarse_index;
  while (peak_left > 0u &&
         coarse.correlations[peak_left - 1u] >= peak_floor) {
    --peak_left;
  }
  while (peak_right + 1u < coarse.correlations.size() &&
         coarse.correlations[peak_right + 1u] >= peak_floor) {
    ++peak_right;
  }
  result.correlation_peak_width_us =
      static_cast<double>(peak_right - peak_left) * kCoarseStepUs;

  if (coarse.best_offset_us == -kMaximumOffsetUs ||
      coarse.best_offset_us == kMaximumOffsetUs ||
      result.correlation < kMinimumCorrelation ||
      result.correlation_peak_width_us > kMaximumPeakWidthUs) {
    result.status = ImuTimeAlignmentStatus::LowConfidence;
    result.message = "the correlation peak is weak, broad, or outside the "
                     "+/-50 ms search range";
    return result;
  }
  result.valid = true;
  result.status = ImuTimeAlignmentStatus::Success;
  result.message = "time offset estimated from synchronized gyroscope motion";
  return result;
}

}  // namespace prism_viewer::dataset
