#pragma once

#include <QtCore/QString>
#include <QtGui/QImage>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace prism_viewer::dataset {

struct DatasetImageEntry {
  uint64_t timestamp_us = 0;
  QString absolute_path;
  uint64_t byte_offset = 0;
  uint32_t byte_size = 0;
  uint32_t exposure_us = 0;
};

struct TumFileSummary {
  uint64_t rows = 0;
  uint64_t first_timestamp_us = 0;
  uint64_t last_timestamp_us = 0;
};

uint64_t parseTumTimestampUs(const std::string& token);
bool loadDatasetImageIndex(const std::filesystem::path& root, size_t camera,
                           std::vector<DatasetImageEntry>* entries,
                           std::string* error);
QImage loadDatasetImage(const DatasetImageEntry& entry);
TumFileSummary summarizeTumFile(const std::filesystem::path& path);

}  // namespace prism_viewer::dataset
