#include "dataset/dataset_browser.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void writeIndex(const std::filesystem::path& path) {
  std::ofstream output(path, std::ios::out | std::ios::trunc);
  output << "# test camera index\n";
  if (!output.good()) throw std::runtime_error("cannot write test index");
}

void expectInspection(const std::filesystem::path& root,
                      size_t expected_count) {
  size_t present_count = 99;
  std::string error;
  if (!prism_viewer::dataset::inspectDatasetCameraIndexes(
          root, 4u, &present_count, &error)) {
    throw std::runtime_error("camera inspection failed: " + error);
  }
  if (present_count != expected_count) {
    throw std::runtime_error("unexpected camera index count");
  }
}

}  // namespace

int main() {
  const auto nonce =
      std::chrono::steady_clock::now().time_since_epoch().count();
  const std::filesystem::path root =
      std::filesystem::temp_directory_path() /
      ("prism-dataset-browser-test-" + std::to_string(nonce));
  std::filesystem::create_directories(root);
  try {
    // Missing all camera indexes is the valid IMU-only dataset layout.
    expectInspection(root, 0u);

    writeIndex(root / "cam0.tum");
    expectInspection(root, 1u);
    for (size_t camera = 1; camera < 4u; ++camera) {
      writeIndex(root / ("cam" + std::to_string(camera) + ".tum"));
    }
    expectInspection(root, 4u);

    std::filesystem::remove(root / "cam3.tum");
    std::filesystem::create_directory(root / "cam3.tum");
    size_t present_count = 0;
    std::string error;
    if (prism_viewer::dataset::inspectDatasetCameraIndexes(
            root, 4u, &present_count, &error) ||
        error.find("not a regular file") == std::string::npos) {
      throw std::runtime_error("non-file camera index was not rejected");
    }

    std::filesystem::remove_all(root);
    std::cout << "dataset browser tests passed\n";
    return 0;
  } catch (const std::exception& exception) {
    std::filesystem::remove_all(root);
    std::cerr << exception.what() << '\n';
    return 1;
  }
}
