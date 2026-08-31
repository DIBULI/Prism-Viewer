#include "dataset/dataset_browser.hpp"

#include "common/ui_text.hpp"
#include "prism/usb/exposure.hpp"

#include <QtCore/QByteArray>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <fstream>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace prism_viewer::dataset {
namespace {

constexpr uint64_t kMinimumRkClockRealtimeUs = 100000000000000ULL;
constexpr uint64_t kMaximumJpegBytes = 64ULL * 1024ULL * 1024ULL;
constexpr uint64_t kMaximumLidarPointsPerBatch = 1000000ULL;
constexpr uint64_t kStoredLidarPointBytes = 16ULL;
constexpr size_t kMaximumReportedIssues = 256u;

struct TimestampRow {
  uint64_t timestamp_us = 0;
  uint64_t line = 0;
};

struct ContainerRange {
  uint64_t begin = 0;
  uint64_t end = 0;
  std::string index_file;
  uint64_t line = 0;
};

using ContainerRanges =
    std::map<std::filesystem::path, std::vector<ContainerRange>>;

bool isSafeRelativePath(const std::filesystem::path& path) {
  if (path.empty() || path.is_absolute()) return false;
  for (const auto& component : path) {
    if (component == std::filesystem::path("..")) return false;
  }
  return true;
}

void addIssue(DatasetValidationResult* result,
              DatasetValidationSeverity severity, const std::string& file,
              uint64_t line, const std::string& message) {
  if (severity == DatasetValidationSeverity::Error) {
    result->valid = false;
    ++result->total_errors;
  } else {
    ++result->total_warnings;
  }
  if (result->issues.size() >= kMaximumReportedIssues) return;
  result->issues.push_back(DatasetValidationIssue{severity, file, line,
                                                   message});
}

uint64_t strictTimestampUs(const std::string& token) {
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
  std::string fraction =
      decimal == std::string::npos ? std::string() : token.substr(decimal + 1u);
  const auto digit = [](char value) {
    return std::isdigit(static_cast<unsigned char>(value)) != 0;
  };
  if (seconds.empty() ||
      !std::all_of(seconds.begin(), seconds.end(), digit) ||
      !std::all_of(fraction.begin(), fraction.end(), digit)) {
    throw std::runtime_error("timestamp contains non-decimal characters");
  }
  if (fraction.size() > 6u) fraction.resize(6u);
  while (fraction.size() < 6u) fraction.push_back('0');
  const uint64_t seconds_value = std::stoull(seconds);
  const uint64_t fraction_value =
      fraction.empty() ? 0u : std::stoull(fraction);
  if (seconds_value >
      (std::numeric_limits<uint64_t>::max() - fraction_value) / 1000000ULL) {
    throw std::runtime_error("timestamp overflows microseconds");
  }
  return seconds_value * 1000000ULL + fraction_value;
}

std::optional<uint64_t> decimalValue(const std::string& text) {
  if (text.empty() ||
      !std::all_of(text.begin(), text.end(), [](char value) {
        return std::isdigit(static_cast<unsigned char>(value)) != 0;
      })) {
    return std::nullopt;
  }
  try {
    return std::stoull(text);
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

std::vector<std::string> splitCsv(const std::string& line) {
  std::vector<std::string> fields;
  std::istringstream input(line);
  for (std::string field; std::getline(input, field, ',');) {
    fields.push_back(std::move(field));
  }
  if (!line.empty() && line.back() == ',') fields.emplace_back();
  return fields;
}

bool signedDecimalValue(const std::string& text, int64_t* value) {
  if (text.empty() || value == nullptr) return false;
  size_t consumed = 0;
  try {
    const int64_t parsed = std::stoll(text, &consumed, 10);
    if (consumed != text.size()) return false;
    *value = parsed;
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

bool finiteDecimalValue(const std::string& text, double* value) {
  if (text.empty() || value == nullptr) return false;
  size_t consumed = 0;
  try {
    const double parsed = std::stod(text, &consumed);
    if (consumed != text.size() || !std::isfinite(parsed)) return false;
    *value = parsed;
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

void analyzeTimestamps(const std::string& file,
                       const std::vector<TimestampRow>& rows,
                       bool regular_cadence,
                       DatasetTimestampSummary* summary,
                       DatasetValidationResult* result) {
  summary->rows = rows.size();
  if (rows.empty()) return;
  summary->first_timestamp_us = rows.front().timestamp_us;
  summary->last_timestamp_us = rows.back().timestamp_us;
  if (rows.size() < 2u) return;

  std::vector<uint64_t> positive_intervals;
  positive_intervals.reserve(rows.size() - 1u);
  for (size_t index = 1; index < rows.size(); ++index) {
    const uint64_t previous = rows[index - 1u].timestamp_us;
    const uint64_t current = rows[index].timestamp_us;
    if (current <= previous) {
      const std::string kind = current == previous ? "repeated" : "moved backward";
      addIssue(result, DatasetValidationSeverity::Error, file,
               rows[index].line,
               "timestamp " + kind + " from " + std::to_string(previous) +
                   " us to " + std::to_string(current) + " us");
      continue;
    }
    positive_intervals.push_back(current - previous);
  }
  if (positive_intervals.empty()) return;
  std::vector<uint64_t> sorted = positive_intervals;
  std::sort(sorted.begin(), sorted.end());
  // Use the lower median so one isolated large interval cannot redefine the
  // nominal cadence in a short recording.
  const uint64_t median = sorted[(sorted.size() - 1u) / 2u];
  summary->median_interval_us = median;
  summary->minimum_interval_us = sorted.front();
  summary->maximum_interval_us = sorted.back();
  if (median == 0u) return;

  std::vector<uint64_t> deviations;
  deviations.reserve(positive_intervals.size());
  for (const uint64_t interval : positive_intervals) {
    deviations.push_back(interval > median ? interval - median
                                           : median - interval);
  }
  std::sort(deviations.begin(), deviations.end());
  const uint64_t median_absolute_deviation =
      deviations[(deviations.size() - 1u) / 2u];
  const uint64_t jitter_allowance = std::max<uint64_t>(
      1000u, std::max<uint64_t>(median / 2u,
                                median_absolute_deviation >
                                        std::numeric_limits<uint64_t>::max() /
                                            6u
                                    ? std::numeric_limits<uint64_t>::max()
                                    : median_absolute_deviation * 6u));
  const uint64_t warning_threshold =
      median > std::numeric_limits<uint64_t>::max() - jitter_allowance
          ? std::numeric_limits<uint64_t>::max()
          : median + jitter_allowance;
  const uint64_t error_threshold =
      std::max<uint64_t>(1000000u,
                         median > std::numeric_limits<uint64_t>::max() / 100u
                             ? std::numeric_limits<uint64_t>::max()
                             : median * 100u);
  size_t interval_index = 0;
  for (size_t row = 1; row < rows.size(); ++row) {
    if (rows[row].timestamp_us <= rows[row - 1u].timestamp_us) continue;
    const uint64_t interval = positive_intervals[interval_index++];
    if (interval >= warning_threshold && interval > median) {
      ++summary->discontinuities;
      const auto severity = interval > error_threshold
                                ? DatasetValidationSeverity::Error
                                : DatasetValidationSeverity::Warning;
      addIssue(result, severity, file, rows[row].line,
               "timestamp jumped forward by " + std::to_string(interval) +
                   " us (median interval " + std::to_string(median) +
                   " us; previous " +
                   std::to_string(rows[row - 1u].timestamp_us) + " us)");
    } else if (regular_cadence && median >= 4u && interval < median / 4u) {
      ++summary->discontinuities;
      addIssue(result, DatasetValidationSeverity::Warning, file,
               rows[row].line,
               "timestamp interval shrank to " + std::to_string(interval) +
                   " us (median interval " + std::to_string(median) + " us)");
    }
  }
}

bool checkCancelled(const DatasetValidationCancelCallback& cancelled,
                    DatasetValidationResult* result) {
  if (!cancelled || !cancelled()) return false;
  result->cancelled = true;
  result->valid = false;
  return true;
}

bool readContainer(const std::filesystem::path& root,
                   const std::string& relative_text, uint64_t offset,
                   uint64_t size, const std::string& index_file,
                   uint64_t line, ContainerRanges* ranges,
                   QByteArray* payload, DatasetValidationResult* result) {
  const std::filesystem::path relative(relative_text);
  if (!isSafeRelativePath(relative)) {
    addIssue(result, DatasetValidationSeverity::Error, index_file, line,
             "container path is absolute or escapes the dataset directory");
    return false;
  }
  if (size == 0u || offset > std::numeric_limits<uint64_t>::max() - size) {
    addIssue(result, DatasetValidationSeverity::Error, index_file, line,
             "container byte range is empty or overflows");
    return false;
  }
  const std::filesystem::path path = (root / relative).lexically_normal();
  std::error_code filesystem_error;
  const uint64_t file_size = std::filesystem::file_size(path, filesystem_error);
  if (filesystem_error) {
    addIssue(result, DatasetValidationSeverity::Error, index_file, line,
             "cannot read container " + relative_text + ": " +
                 filesystem_error.message());
    return false;
  }
  if (offset + size > file_size) {
    addIssue(result, DatasetValidationSeverity::Error, index_file, line,
             "container range " + std::to_string(offset) + ".." +
                 std::to_string(offset + size) + " exceeds " + relative_text +
                 " size " + std::to_string(file_size));
    return false;
  }
  (*ranges)[path].push_back(
      ContainerRange{offset, offset + size, index_file, line});
  if (size > static_cast<uint64_t>(std::numeric_limits<qsizetype>::max())) {
    addIssue(result, DatasetValidationSeverity::Error, index_file, line,
             "container record exceeds host memory limit");
    return false;
  }
  std::ifstream input(path, std::ios::in | std::ios::binary);
  if (!input.is_open() ||
      offset > static_cast<uint64_t>(
                   std::numeric_limits<std::streamoff>::max())) {
    addIssue(result, DatasetValidationSeverity::Error, index_file, line,
             "cannot open or seek container " + relative_text);
    return false;
  }
  input.seekg(static_cast<std::streamoff>(offset));
  payload->resize(static_cast<qsizetype>(size));
  input.read(payload->data(), static_cast<std::streamsize>(size));
  if (input.gcount() != static_cast<std::streamsize>(size)) {
    addIssue(result, DatasetValidationSeverity::Error, index_file, line,
             "short read from container " + relative_text);
    payload->clear();
    return false;
  }
  return true;
}

void checkOverlappingRanges(ContainerRanges* ranges,
                            DatasetValidationResult* result) {
  for (auto& [path, entries] : *ranges) {
    std::sort(entries.begin(), entries.end(),
              [](const ContainerRange& left, const ContainerRange& right) {
                if (left.begin != right.begin) return left.begin < right.begin;
                return left.end < right.end;
              });
    for (size_t index = 1; index < entries.size(); ++index) {
      if (entries[index].begin >= entries[index - 1u].end) continue;
      addIssue(result, DatasetValidationSeverity::Error,
               entries[index].index_file, entries[index].line,
               "container byte range overlaps an earlier record in " +
                   path.filename().string());
    }
  }
}

}  // namespace

size_t DatasetValidationResult::errorCount() const {
  return total_errors;
}

size_t DatasetValidationResult::warningCount() const {
  return total_warnings;
}

uint64_t parseTumTimestampUs(const std::string& token) {
  const size_t decimal = token.find('.');
  const std::string seconds_text =
      decimal == std::string::npos ? token : token.substr(0, decimal);
  std::string fraction =
      decimal == std::string::npos ? std::string() : token.substr(decimal + 1);
  if (fraction.size() > 6) fraction.resize(6);
  while (fraction.size() < 6) fraction.push_back('0');
  const uint64_t seconds =
      seconds_text.empty() ? 0 : std::stoull(seconds_text);
  const uint64_t microseconds =
      fraction.empty() ? 0 : std::stoull(fraction);
  return seconds * 1000000ULL + microseconds;
}

bool inspectDatasetCameraIndexes(const std::filesystem::path& root,
                                 size_t camera_count,
                                 size_t* present_count,
                                 std::string* error) {
  if (present_count == nullptr) {
    if (error != nullptr) *error = "camera index count output is required";
    return false;
  }

  *present_count = 0;
  if (error != nullptr) error->clear();
  for (size_t camera = 0; camera < camera_count; ++camera) {
    const std::filesystem::path path =
        root / ("cam" + std::to_string(camera) + ".tum");
    std::error_code filesystem_error;
    const std::filesystem::file_status status =
        std::filesystem::status(path, filesystem_error);
    if (filesystem_error) {
      if (filesystem_error == std::errc::no_such_file_or_directory) {
        continue;
      }
      if (error != nullptr) {
        *error = "cannot inspect " + path.filename().string() + ": " +
                 filesystem_error.message();
      }
      return false;
    }
    if (std::filesystem::is_regular_file(status)) {
      ++*present_count;
      continue;
    }
    if (std::filesystem::exists(status)) {
      if (error != nullptr) {
        *error = path.filename().string() + " is not a regular file";
      }
      return false;
    }
  }
  return true;
}

bool loadDatasetImageIndex(const std::filesystem::path& root, size_t camera,
                           std::vector<DatasetImageEntry>* entries,
                           std::string* error) {
  entries->clear();
  std::ifstream input(root / ("cam" + std::to_string(camera) + ".tum"));
  if (!input.is_open()) {
    if (error != nullptr) {
      *error = "cannot open cam" + std::to_string(camera) + ".tum";
    }
    return false;
  }

  std::string line;
  uint64_t line_number = 0;
  try {
    while (std::getline(input, line)) {
      ++line_number;
      if (line.empty() || line[0] == '#') continue;
      std::istringstream parser(line);
      std::string timestamp;
      std::string relative_path;
      uint64_t byte_offset = 0;
      uint64_t byte_size = 0;
      uint64_t exposure_us = 0;
      std::string trailing;
      if (!(parser >> timestamp >> relative_path >> byte_offset >> byte_size >>
            exposure_us) ||
          (parser >> trailing)) {
        throw std::runtime_error(
            "expected timestamp container_path byte_offset byte_size "
            "actual_exposure_us");
      }
      DatasetImageEntry entry;
      entry.timestamp_us = parseTumTimestampUs(timestamp);
      entry.absolute_path = common::fromFilesystemPath(
          root / std::filesystem::path(relative_path));
      if (byte_size == 0 ||
          byte_size >
              static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())) {
        throw std::runtime_error("invalid container JPEG size");
      }
      entry.byte_offset = byte_offset;
      entry.byte_size = static_cast<uint32_t>(byte_size);
      if (exposure_us < prism::kCameraMinExposureUs ||
          exposure_us > prism::kCameraMaxExposureUs) {
        throw std::runtime_error("invalid actual exposure time");
      }
      entry.exposure_us = static_cast<uint32_t>(exposure_us);
      entries->push_back(std::move(entry));
    }
  } catch (const std::exception& ex) {
    if (error != nullptr) {
      *error = "invalid camera index line " + std::to_string(line_number) +
               ": " + ex.what();
    }
    entries->clear();
    return false;
  }
  if (entries->empty()) {
    if (error != nullptr) {
      *error = "cam" + std::to_string(camera) + ".tum has no images";
    }
    return false;
  }
  return true;
}

QImage loadDatasetImage(const DatasetImageEntry& entry) {
  std::ifstream input(common::toFilesystemPath(entry.absolute_path),
                      std::ios::in | std::ios::binary);
  if (!input.is_open()) return {};
  input.seekg(static_cast<std::streamoff>(entry.byte_offset));
  if (!input.good()) return {};
  QByteArray encoded(static_cast<qsizetype>(entry.byte_size), Qt::Uninitialized);
  input.read(encoded.data(), static_cast<std::streamsize>(entry.byte_size));
  if (input.gcount() != static_cast<std::streamsize>(entry.byte_size)) return {};
  return QImage::fromData(encoded, "JPG");
}

TumFileSummary summarizeTumFile(const std::filesystem::path& path) {
  TumFileSummary summary;
  std::ifstream input(path);
  std::string line;
  while (std::getline(input, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream parser(line);
    std::string timestamp;
    if (!(parser >> timestamp)) continue;
    try {
      const uint64_t timestamp_us = parseTumTimestampUs(timestamp);
      if (summary.rows == 0) summary.first_timestamp_us = timestamp_us;
      summary.last_timestamp_us = timestamp_us;
      ++summary.rows;
    } catch (const std::exception&) {
      continue;
    }
  }
  return summary;
}

TumFileSummary summarizeGpsRtkFile(const std::filesystem::path& path) {
  TumFileSummary summary;
  std::ifstream input(path);
  for (std::string line; std::getline(input, line);) {
    if (line.empty() || line.front() == '#' ||
        line.rfind("host_receive_unix_us,", 0u) == 0u) {
      continue;
    }
    const size_t separator = line.find(',');
    const auto timestamp = decimalValue(line.substr(0, separator));
    if (!timestamp) continue;
    if (summary.rows == 0u) summary.first_timestamp_us = *timestamp;
    summary.last_timestamp_us = *timestamp;
    ++summary.rows;
  }
  return summary;
}

DatasetValidationResult validatePrismDatasetImpl(
    const std::filesystem::path& root,
    const DatasetValidationProgressCallback& progress,
    const DatasetValidationCancelCallback& cancelled) {
  DatasetValidationResult result;
  result.valid = true;
  if (root.empty() || !std::filesystem::is_directory(root)) {
    addIssue(&result, DatasetValidationSeverity::Error, {}, 0,
             "dataset directory does not exist");
    return result;
  }

  std::map<std::string, std::string> manifest;
  const std::filesystem::path manifest_path = root / "dataset.info";
  if (std::filesystem::is_regular_file(manifest_path)) {
    std::ifstream input(manifest_path);
    uint64_t line_number = 0;
    for (std::string line; std::getline(input, line);) {
      ++line_number;
      if (line.empty() || line.front() == '#') continue;
      const size_t separator = line.find('=');
      if (separator == std::string::npos || separator == 0u ||
          !manifest.emplace(line.substr(0, separator),
                            line.substr(separator + 1u))
               .second) {
        addIssue(&result, DatasetValidationSeverity::Error, "dataset.info",
                 line_number, "invalid or duplicate manifest field");
      }
    }
    if (!input.eof()) {
      addIssue(&result, DatasetValidationSeverity::Error, "dataset.info", 0,
               "failed while reading manifest");
    }
  } else {
    addIssue(&result, DatasetValidationSeverity::Warning, "dataset.info", 0,
             "manifest is missing; treating dataset as a legacy layout");
  }

  const auto format = manifest.find("format");
  if (format != manifest.end()) {
    constexpr const char* kPrefix = "prism-dataset-v";
    if (format->second.rfind(kPrefix, 0u) != 0u) {
      addIssue(&result, DatasetValidationSeverity::Error, "dataset.info", 0,
               "unsupported dataset format declaration");
    } else {
      const auto version =
          decimalValue(format->second.substr(std::char_traits<char>::length(
              kPrefix)));
      if (!version || *version > std::numeric_limits<uint32_t>::max()) {
        addIssue(&result, DatasetValidationSeverity::Error, "dataset.info", 0,
                 "invalid dataset format version");
      } else {
        result.format_version = static_cast<uint32_t>(*version);
        if (result.format_version > 6u) {
          addIssue(&result, DatasetValidationSeverity::Error, "dataset.info",
                   0, "dataset format is newer than this Viewer supports");
        }
      }
    }
  }
  const bool strict_v6 = result.format_version == 6u;
  const auto field = [&manifest](const char* name) -> std::string {
    const auto found = manifest.find(name);
    return found == manifest.end() ? std::string() : found->second;
  };
  const auto checkManifestCount =
      [&manifest, &result](const std::string& name, uint64_t actual) {
        if (result.format_version != 6u) return;
        const auto found = manifest.find(name);
        if (found == manifest.end()) {
          addIssue(&result, DatasetValidationSeverity::Error, "dataset.info",
                   0, "v6 manifest is missing " + name);
          return;
        }
        const auto declared = decimalValue(found->second);
        if (!declared || *declared != actual) {
          addIssue(&result, DatasetValidationSeverity::Error, "dataset.info",
                   0, name + " declares " + found->second +
                          " but the indexes contain " +
                          std::to_string(actual));
        }
      };
  const auto checkOptionalManifestCount =
      [&manifest, &result](const std::string& name, uint64_t actual) {
        const auto found = manifest.find(name);
        if (found == manifest.end()) return;
        const auto declared = decimalValue(found->second);
        if (!declared || *declared != actual) {
          addIssue(&result, DatasetValidationSeverity::Error, "dataset.info",
                   0, name + " declares " + found->second +
                          " but gps_rtk.csv contains " +
                          std::to_string(actual));
        }
      };
  result.recording_mode = field("recording_mode");
  if (strict_v6) {
    const std::array<const char*, 8> required = {
        "complete",       "recording_mode", "image_storage",
        "camera_index",   "lidar_storage",  "lidar_imu_storage",
        "time_domain",    "timestamp_epoch"};
    for (const char* key : required) {
      if (manifest.find(key) == manifest.end()) {
        addIssue(&result, DatasetValidationSeverity::Error, "dataset.info", 0,
                 std::string("v6 manifest is missing ") + key);
      }
    }
    if (field("complete") != "1") {
      addIssue(&result, DatasetValidationSeverity::Error, "dataset.info", 0,
               "recording is incomplete (complete is not 1)");
    }
    if (result.recording_mode != "full" &&
        result.recording_mode != "imu-only") {
      addIssue(&result, DatasetValidationSeverity::Error, "dataset.info", 0,
               "recording_mode must be full or imu-only");
    }
    if (field("time_domain") != "rk-clock-realtime" ||
        field("timestamp_epoch") != "unix" ||
        field("alignment") != "common-device-time-domain") {
      addIssue(&result, DatasetValidationSeverity::Error, "dataset.info", 0,
               "v6 time-domain declaration is missing or unsupported");
    }
  }

  size_t camera_index_count = 0;
  for (size_t camera = 0; camera < result.cameras.size(); ++camera) {
    const auto path = root / ("cam" + std::to_string(camera) + ".tum");
    if (std::filesystem::is_regular_file(path)) ++camera_index_count;
  }
  if (camera_index_count != 0u &&
      camera_index_count != result.cameras.size()) {
    addIssue(&result, DatasetValidationSeverity::Error, {}, 0,
             "camera indexes are incomplete; expected cam0.tum through cam3.tum");
  }
  result.cameras_present = camera_index_count == result.cameras.size();
  if (strict_v6) {
    const bool declared_cameras =
        field("image_storage") == "chunk-v1" &&
        field("camera_index") == "chunk-v2-with-actual-exposure";
    if (declared_cameras != result.cameras_present ||
        (result.recording_mode == "full") != declared_cameras) {
      addIssue(&result, DatasetValidationSeverity::Error, "dataset.info", 0,
               "camera files do not match the v6 recording declaration");
    }
  }

  ContainerRanges ranges;
  std::array<std::vector<TimestampRow>, 4> camera_timestamps;
  if (result.cameras_present) {
    for (size_t camera = 0; camera < result.cameras.size(); ++camera) {
      const std::string filename = "cam" + std::to_string(camera) + ".tum";
      std::ifstream input(root / filename);
      uint64_t line_number = 0;
      for (std::string line; std::getline(input, line);) {
        ++line_number;
        if (line.empty() || line.front() == '#') continue;
        if (checkCancelled(cancelled, &result)) return result;
        std::istringstream parser(line);
        std::string timestamp_text;
        std::string container;
        uint64_t offset = 0;
        uint64_t size = 0;
        uint64_t exposure_us = 0;
        std::string trailing;
        if (!(parser >> timestamp_text >> container >> offset >> size >>
              exposure_us) ||
            (parser >> trailing) || size == 0u || size > kMaximumJpegBytes ||
            exposure_us < prism::kCameraMinExposureUs ||
            exposure_us > prism::kCameraMaxExposureUs) {
          addIssue(&result, DatasetValidationSeverity::Error, filename,
                   line_number,
                   "invalid camera index row or exposure value");
          continue;
        }
        uint64_t timestamp_us = 0;
        try {
          timestamp_us = strictTimestampUs(timestamp_text);
        } catch (const std::exception& error) {
          addIssue(&result, DatasetValidationSeverity::Error, filename,
                   line_number, error.what());
          continue;
        }
        if (strict_v6 && timestamp_us < kMinimumRkClockRealtimeUs) {
          addIssue(&result, DatasetValidationSeverity::Error, filename,
                   line_number,
                   "timestamp is outside the declared RK CLOCK_REALTIME epoch");
        }
        camera_timestamps[camera].push_back({timestamp_us, line_number});
        QByteArray jpeg;
        if (readContainer(root, container, offset, size, filename, line_number,
                          &ranges, &jpeg, &result) &&
            QImage::fromData(jpeg, "JPG").isNull()) {
          addIssue(&result, DatasetValidationSeverity::Error, filename,
                   line_number, "indexed payload is not a decodable JPEG");
        }
        ++result.checked_records;
        if (progress && result.checked_records % 256u == 0u) {
          progress(result.checked_records, filename);
        }
      }
      if (!input.eof()) {
        addIssue(&result, DatasetValidationSeverity::Error, filename, 0,
                 "failed while reading camera index");
      }
      analyzeTimestamps(filename, camera_timestamps[camera], true,
                        &result.cameras[camera], &result);
      checkManifestCount("camera" + std::to_string(camera) + "_images",
                         result.cameras[camera].rows);
    }
    const size_t expected_rows = camera_timestamps.front().size();
    for (size_t camera = 1; camera < camera_timestamps.size(); ++camera) {
      if (camera_timestamps[camera].size() != expected_rows) {
        addIssue(&result, DatasetValidationSeverity::Error,
                 "cam" + std::to_string(camera) + ".tum", 0,
                 "camera indexes contain different frame counts");
        continue;
      }
      for (size_t row = 0; row < expected_rows; ++row) {
        if (camera_timestamps[camera][row].timestamp_us !=
            camera_timestamps[0][row].timestamp_us) {
          addIssue(&result, DatasetValidationSeverity::Error,
                   "cam" + std::to_string(camera) + ".tum",
                   camera_timestamps[camera][row].line,
                   "four-camera frame timestamps are not identical");
          break;
        }
      }
    }
  }

  for (size_t imu = 0; imu < result.onboard_imus.size(); ++imu) {
    const std::string filename = "imu" + std::to_string(imu) + ".tum";
    std::ifstream input(root / filename);
    if (!input.is_open()) {
      addIssue(&result, DatasetValidationSeverity::Error, filename, 0,
               "required onboard IMU file is missing");
      continue;
    }
    std::vector<TimestampRow> timestamps;
    uint64_t line_number = 0;
    for (std::string line; std::getline(input, line);) {
      ++line_number;
      if (line.empty() || line.front() == '#') continue;
      if (checkCancelled(cancelled, &result)) return result;
      std::istringstream parser(line);
      std::string timestamp_text;
      std::array<double, 6> values{};
      std::string trailing;
      if (!(parser >> timestamp_text >> values[0] >> values[1] >> values[2] >>
            values[3] >> values[4] >> values[5]) ||
          (parser >> trailing) ||
          !std::all_of(values.begin(), values.end(),
                       [](double value) { return std::isfinite(value); })) {
        addIssue(&result, DatasetValidationSeverity::Error, filename,
                 line_number, "invalid onboard IMU row");
        continue;
      }
      try {
        const uint64_t timestamp_us = strictTimestampUs(timestamp_text);
        if (strict_v6 && timestamp_us < kMinimumRkClockRealtimeUs) {
          throw std::runtime_error(
              "timestamp is outside the declared RK CLOCK_REALTIME epoch");
        }
        timestamps.push_back({timestamp_us, line_number});
      } catch (const std::exception& error) {
        addIssue(&result, DatasetValidationSeverity::Error, filename,
                 line_number, error.what());
      }
      ++result.checked_records;
    }
    analyzeTimestamps(filename, timestamps, true,
                      &result.onboard_imus[imu], &result);
    checkManifestCount("imu" + std::to_string(imu) + "_samples",
                       result.onboard_imus[imu].rows);
  }
  if (std::none_of(result.onboard_imus.begin(), result.onboard_imus.end(),
                   [](const auto& stream) { return stream.rows != 0u; })) {
    addIssue(&result, DatasetValidationSeverity::Error, "imu0.tum", 0,
             "all onboard IMU streams are empty");
  }

  const std::filesystem::path lidar_path = root / "lidar.tum";
  result.lidar_present = std::filesystem::is_regular_file(lidar_path);
  if (strict_v6) {
    const bool declared =
        field("lidar_storage") ==
        "cartesian-mm-chunk-v2-with-time-source";
    if (declared != result.lidar_present ||
        (result.recording_mode == "imu-only" && declared)) {
      addIssue(&result, DatasetValidationSeverity::Error, "dataset.info", 0,
               "LiDAR point files do not match the v6 declaration");
    }
  }
  if (result.lidar_present) {
    std::ifstream input(lidar_path);
    std::vector<TimestampRow> timestamps;
    uint64_t line_number = 0;
    uint64_t total_points = 0;
    for (std::string line; std::getline(input, line);) {
      ++line_number;
      if (line.empty() || line.front() == '#') continue;
      if (checkCancelled(cancelled, &result)) return result;
      std::istringstream parser(line);
      std::string timestamp_text;
      std::string container;
      uint64_t offset = 0;
      uint64_t size = 0;
      uint64_t point_count = 0;
      uint32_t model = 0;
      uint32_t device_type = 0;
      uint32_t time_type = 0;
      uint64_t batch_id = 0;
      uint64_t raw_timestamp = 0;
      if (!(parser >> timestamp_text >> container >> offset >> size >>
            point_count >> model >> device_type >> time_type >> batch_id >>
            raw_timestamp) ||
          point_count == 0u || point_count > kMaximumLidarPointsPerBatch ||
          size != point_count * kStoredLidarPointBytes ||
          (model != 1u && model != 2u)) {
        addIssue(&result, DatasetValidationSeverity::Error, "lidar.tum",
                 line_number, "invalid LiDAR point index row");
        continue;
      }
      bool has_time_source = false;
      uint32_t interval = 0;
      uint32_t synced = 0;
      uint32_t tai = 0;
      parser >> std::ws;
      if (!parser.eof()) {
        std::string trailing;
        if (!(parser >> interval >> synced >> tai) || (parser >> trailing) ||
            interval > std::numeric_limits<uint16_t>::max() || synced > 1u ||
            tai > 1u || (tai != 0u && synced == 0u)) {
          addIssue(&result, DatasetValidationSeverity::Error, "lidar.tum",
                   line_number, "invalid LiDAR time-source fields");
          continue;
        }
        has_time_source = true;
      }
      if (strict_v6 && (!has_time_source || synced != 1u)) {
        addIssue(&result, DatasetValidationSeverity::Error, "lidar.tum",
                 line_number, "v6 LiDAR timestamp is not synchronized");
      }
      try {
        const uint64_t timestamp_us = strictTimestampUs(timestamp_text);
        if (strict_v6 && timestamp_us < kMinimumRkClockRealtimeUs) {
          throw std::runtime_error(
              "timestamp is outside the declared RK CLOCK_REALTIME epoch");
        }
        timestamps.push_back({timestamp_us, line_number});
      } catch (const std::exception& error) {
        addIssue(&result, DatasetValidationSeverity::Error, "lidar.tum",
                 line_number, error.what());
      }
      QByteArray payload;
      (void)readContainer(root, container, offset, size, "lidar.tum",
                          line_number, &ranges, &payload, &result);
      if (total_points > std::numeric_limits<uint64_t>::max() - point_count) {
        addIssue(&result, DatasetValidationSeverity::Error, "lidar.tum",
                 line_number, "total LiDAR point count overflows");
      } else {
        total_points += point_count;
      }
      ++result.checked_records;
    }
    analyzeTimestamps("lidar.tum", timestamps, false, &result.lidar,
                      &result);
    if (strict_v6 && timestamps.empty()) {
      addIssue(&result, DatasetValidationSeverity::Error, "lidar.tum", 0,
               "declared LiDAR point stream is empty");
    }
    checkManifestCount("lidar_batches", result.lidar.rows);
    checkManifestCount("lidar_points", total_points);
  } else {
    checkManifestCount("lidar_batches", 0u);
    checkManifestCount("lidar_points", 0u);
  }

  const std::filesystem::path lidar_imu_path = root / "lidar_imu.tum";
  result.lidar_imu_present = std::filesystem::is_regular_file(lidar_imu_path);
  if (strict_v6) {
    const bool declared =
        field("lidar_imu_storage") == "tum-si-v2-with-time-source";
    if (declared != result.lidar_imu_present) {
      addIssue(&result, DatasetValidationSeverity::Error, "dataset.info", 0,
               "LiDAR IMU file does not match the v6 declaration");
    }
    if (result.recording_mode == "full" &&
        result.lidar_present != result.lidar_imu_present) {
      addIssue(&result, DatasetValidationSeverity::Error, "dataset.info", 0,
               "full v6 recording must contain both LiDAR point and IMU streams");
    }
  }
  if (result.lidar_imu_present) {
    std::ifstream input(lidar_imu_path);
    std::vector<TimestampRow> timestamps;
    uint64_t line_number = 0;
    for (std::string line; std::getline(input, line);) {
      ++line_number;
      if (line.empty() || line.front() == '#') continue;
      if (checkCancelled(cancelled, &result)) return result;
      std::istringstream parser(line);
      std::string timestamp_text;
      std::array<double, 6> values{};
      if (!(parser >> timestamp_text >> values[0] >> values[1] >> values[2] >>
            values[3] >> values[4] >> values[5]) ||
          !std::all_of(values.begin(), values.end(),
                       [](double value) { return std::isfinite(value); })) {
        addIssue(&result, DatasetValidationSeverity::Error, "lidar_imu.tum",
                 line_number, "invalid LiDAR IMU row");
        continue;
      }
      bool has_source = false;
      bool has_tai = false;
      uint32_t synced = 0;
      parser >> std::ws;
      if (!parser.eof()) {
        uint32_t model = 0;
        uint32_t device_type = 0;
        uint32_t time_type = 0;
        uint64_t sample_id = 0;
        uint64_t raw_timestamp = 0;
        if (!(parser >> model >> device_type >> time_type >> sample_id >>
              raw_timestamp >> synced) ||
            (model != 1u && model != 2u) || device_type > 255u ||
            time_type > 255u || sample_id > std::numeric_limits<uint32_t>::max() ||
            synced > 1u) {
          addIssue(&result, DatasetValidationSeverity::Error,
                   "lidar_imu.tum", line_number,
                   "invalid LiDAR IMU source fields");
          continue;
        }
        has_source = true;
        parser >> std::ws;
        if (!parser.eof()) {
          uint32_t tai = 0;
          std::string trailing;
          if (!(parser >> tai) || (parser >> trailing) || tai > 1u ||
              (tai != 0u && synced == 0u)) {
            addIssue(&result, DatasetValidationSeverity::Error,
                     "lidar_imu.tum", line_number,
                     "invalid LiDAR IMU TAI flag");
            continue;
          }
          has_tai = true;
        }
      }
      if (strict_v6 && (!has_source || !has_tai || synced != 1u)) {
        addIssue(&result, DatasetValidationSeverity::Error,
                 "lidar_imu.tum", line_number,
                 "v6 LiDAR IMU timestamp is not synchronized");
      }
      try {
        const uint64_t timestamp_us = strictTimestampUs(timestamp_text);
        if (strict_v6 && timestamp_us < kMinimumRkClockRealtimeUs) {
          throw std::runtime_error(
              "timestamp is outside the declared RK CLOCK_REALTIME epoch");
        }
        timestamps.push_back({timestamp_us, line_number});
      } catch (const std::exception& error) {
        addIssue(&result, DatasetValidationSeverity::Error,
                 "lidar_imu.tum", line_number, error.what());
      }
      ++result.checked_records;
    }
    analyzeTimestamps("lidar_imu.tum", timestamps, true, &result.lidar_imu,
                      &result);
    if (strict_v6 && timestamps.empty()) {
      addIssue(&result, DatasetValidationSeverity::Error, "lidar_imu.tum", 0,
               "declared LiDAR IMU stream is empty");
    }
    checkManifestCount("lidar_imu_samples", result.lidar_imu.rows);
  } else {
    checkManifestCount("lidar_imu_samples", 0u);
  }

  const std::filesystem::path gps_rtk_path = root / "gps_rtk.csv";
  result.gps_rtk_present = std::filesystem::is_regular_file(gps_rtk_path);
  const bool gps_manifest_present =
      manifest.find("gps_rtk_storage") != manifest.end();
  if (gps_manifest_present) {
    if (field("gps_rtk_storage") != "csv-v1" ||
        !result.gps_rtk_present) {
      addIssue(&result, DatasetValidationSeverity::Error, "dataset.info", 0,
               "GPS/RTK file does not match the manifest declaration");
    }
  } else if (result.gps_rtk_present && strict_v6) {
    addIssue(&result, DatasetValidationSeverity::Error, "dataset.info", 0,
             "v6 dataset with gps_rtk.csv is missing gps_rtk_storage");
  }
  if (result.gps_rtk_present) {
    std::ifstream input(gps_rtk_path);
    std::vector<TimestampRow> timestamps;
    uint64_t line_number = 0;
    bool header_seen = false;
    for (std::string line; std::getline(input, line);) {
      ++line_number;
      if (line.empty() || line.front() == '#') continue;
      if (!header_seen) {
        header_seen = true;
        if (line.rfind("host_receive_unix_us,navigation_valid,", 0u) != 0u) {
          addIssue(&result, DatasetValidationSeverity::Error, "gps_rtk.csv",
                   line_number, "unsupported GPS/RTK CSV header");
        }
        continue;
      }
      if (checkCancelled(cancelled, &result)) return result;
      const std::vector<std::string> columns = splitCsv(line);
      if (columns.size() != 41u) {
        addIssue(&result, DatasetValidationSeverity::Error, "gps_rtk.csv",
                 line_number, "GPS/RTK row must contain 41 columns");
        continue;
      }
      std::array<std::optional<uint64_t>, 26> unsigned_values;
      const std::array<size_t, 26> unsigned_columns = {
          0u, 1u, 2u, 3u, 5u, 6u, 8u, 9u, 16u, 19u, 21u, 22u, 23u,
          26u, 27u, 28u, 29u, 30u, 31u, 32u, 33u, 34u, 35u, 36u, 37u,
          38u};
      bool numeric_ok = true;
      for (size_t index = 0; index < unsigned_columns.size(); ++index) {
        unsigned_values[index] = decimalValue(columns[unsigned_columns[index]]);
        numeric_ok = numeric_ok && unsigned_values[index].has_value();
      }
      int64_t base_station_id = 0;
      int64_t correction_error = 0;
      int64_t navigation_error = 0;
      numeric_ok = numeric_ok &&
                   signedDecimalValue(columns[25], &base_station_id) &&
                   signedDecimalValue(columns[39], &correction_error) &&
                   signedDecimalValue(columns[40], &navigation_error);
      std::array<double, 9> measurements{};
      const std::array<size_t, 9> measurement_columns = {
          10u, 11u, 12u, 13u, 14u, 15u, 17u, 18u, 20u};
      for (size_t index = 0; index < measurement_columns.size(); ++index) {
        numeric_ok = numeric_ok && finiteDecimalValue(
            columns[measurement_columns[index]], &measurements[index]);
      }
      if (!numeric_ok) {
        addIssue(&result, DatasetValidationSeverity::Error, "gps_rtk.csv",
                 line_number, "GPS/RTK row contains an invalid number");
        continue;
      }
      const uint64_t host_timestamp_us = *unsigned_values[0];
      const uint64_t navigation_valid = *unsigned_values[1];
      const uint64_t solution_epoch_us = *unsigned_values[2];
      const uint64_t solution = *unsigned_values[3];
      const uint64_t confidence_valid = *unsigned_values[4];
      const uint64_t confidence = *unsigned_values[5];
      const uint64_t confidence_score = *unsigned_values[6];
      const uint64_t satellites = *unsigned_values[8];
      const uint64_t position_jump_valid = *unsigned_values[9];
      const uint64_t base_source = *unsigned_values[12];
      const uint64_t base_position_valid = *unsigned_values[13];
      const uint64_t host_active = *unsigned_values[14];
      const uint64_t ntrip_connected = *unsigned_values[15];
      if (host_timestamp_us < kMinimumRkClockRealtimeUs ||
          navigation_valid > 1u || confidence_valid > 1u ||
          position_jump_valid > 1u || base_position_valid > 1u ||
          host_active > 1u || ntrip_connected > 1u || solution > 5u ||
          confidence > 3u || confidence_score > 1000u ||
          satellites > std::numeric_limits<uint16_t>::max() ||
          base_source > 3u || columns[4].empty() || columns[7].empty() ||
          columns[24].empty()) {
        addIssue(&result, DatasetValidationSeverity::Error, "gps_rtk.csv",
                 line_number, "GPS/RTK enum, flag, or timestamp is invalid");
        continue;
      }
      if (navigation_valid != 0u &&
          (solution_epoch_us < kMinimumRkClockRealtimeUs || solution == 0u ||
           measurements[0] < -90.0 || measurements[0] > 90.0 ||
           measurements[1] < -180.0 || measurements[1] > 180.0 ||
           measurements[3] < 0.0 || measurements[4] < 0.0 ||
           measurements[5] < 0.0 || measurements[6] < 0.0 ||
           measurements[7] < 0.0 ||
           (position_jump_valid != 0u && measurements[8] < 0.0))) {
        addIssue(&result, DatasetValidationSeverity::Error, "gps_rtk.csv",
                 line_number, "GPS/RTK navigation solution is invalid");
        continue;
      }
      if ((confidence_valid != 0u && confidence == 0u) ||
          (navigation_valid == 0u &&
           (confidence_valid != 0u || position_jump_valid != 0u))) {
        addIssue(&result, DatasetValidationSeverity::Error, "gps_rtk.csv",
                 line_number, "GPS/RTK validity flags are inconsistent");
        continue;
      }
      timestamps.push_back({host_timestamp_us, line_number});
      if (navigation_valid != 0u) ++result.gps_rtk_navigation_samples;
      ++result.checked_records;
    }
    if (!input.eof()) {
      addIssue(&result, DatasetValidationSeverity::Error, "gps_rtk.csv", 0,
               "failed while reading GPS/RTK CSV");
    }
    if (!header_seen) {
      addIssue(&result, DatasetValidationSeverity::Error, "gps_rtk.csv", 0,
               "GPS/RTK CSV header is missing");
    }
    analyzeTimestamps("gps_rtk.csv", timestamps, false, &result.gps_rtk,
                      &result);
    checkOptionalManifestCount("gps_rtk_samples", result.gps_rtk.rows);
    checkOptionalManifestCount("gps_rtk_navigation_samples",
                               result.gps_rtk_navigation_samples);
  } else {
    checkOptionalManifestCount("gps_rtk_samples", 0u);
    checkOptionalManifestCount("gps_rtk_navigation_samples", 0u);
  }

  checkOverlappingRanges(&ranges, &result);
  if (!result.cameras_present && result.onboard_imus[0].rows == 0u &&
      result.onboard_imus[1].rows == 0u && result.lidar.rows == 0u &&
      result.lidar_imu.rows == 0u) {
    addIssue(&result, DatasetValidationSeverity::Error, {}, 0,
             "dataset contains no sensor records");
  }
  if (progress) progress(result.checked_records, "complete");
  result.valid = result.errorCount() == 0u && !result.cancelled;
  return result;
}

DatasetValidationResult validatePrismDataset(
    const std::filesystem::path& root,
    const DatasetValidationProgressCallback& progress,
    const DatasetValidationCancelCallback& cancelled) {
  try {
    return validatePrismDatasetImpl(root, progress, cancelled);
  } catch (const std::exception& error) {
    DatasetValidationResult result;
    addIssue(&result, DatasetValidationSeverity::Error, {}, 0,
             std::string("dataset validation failed: ") + error.what());
    return result;
  } catch (...) {
    DatasetValidationResult result;
    addIssue(&result, DatasetValidationSeverity::Error, {}, 0,
             "dataset validation failed unexpectedly");
    return result;
  }
}

}  // namespace prism_viewer::dataset
