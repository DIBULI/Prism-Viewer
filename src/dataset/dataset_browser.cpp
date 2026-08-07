#include "dataset/dataset_browser.hpp"

#include "common/ui_text.hpp"
#include "prism/usb/exposure.hpp"

#include <QtCore/QByteArray>

#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace prism_viewer::dataset {

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

}  // namespace prism_viewer::dataset
