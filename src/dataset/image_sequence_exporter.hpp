#pragma once
#include "dataset/dataset_browser.hpp"
#include <map>

namespace prism_viewer::dataset {
struct ImageSequenceIndex {
  std::array<std::vector<DatasetImageEntry>,4> cameras;
  std::map<std::string,std::string> metadata;
  bool profile = false;
};
struct ImageSequenceExportResult {
  bool success = false, cancelled = false;
  std::array<uint64_t,4> images{};
  std::string error;
};
// Reads bounded indexes, not image pixels. Each camera retains its own ordering.
ImageSequenceIndex inspectImageSequence(const std::filesystem::path& root,
    const DatasetValidationCancelCallback& cancelled = {});
// The output must be a NEW directory outside the source dataset. Failure/cancel
// removes only this operation's private staging directory, never source files.
ImageSequenceExportResult exportImageSequence(const std::filesystem::path& root,
    const std::filesystem::path& output,
    const DatasetValidationProgressCallback& progress = {},
    const DatasetValidationCancelCallback& cancelled = {});
}
