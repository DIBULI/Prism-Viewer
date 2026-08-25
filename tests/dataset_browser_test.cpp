#include "dataset/dataset_browser.hpp"

#include <chrono>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

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

void writeText(const std::filesystem::path& path, const std::string& text) {
  std::ofstream output(path, std::ios::out | std::ios::trunc);
  output << text;
  if (!output.good()) throw std::runtime_error("cannot write test file");
}

void writeBytes(const std::filesystem::path& path,
                const std::vector<uint8_t>& bytes) {
  std::ofstream output(path,
                       std::ios::out | std::ios::trunc | std::ios::binary);
  output.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  if (!output.good()) throw std::runtime_error("cannot write test container");
}

void createImuOnlyDataset(const std::filesystem::path& root,
                          const std::string& imu0_rows,
                          const std::string& imu1_rows) {
  const auto rows = [](const std::string& text) {
    uint64_t count = 0;
    std::istringstream input(text);
    for (std::string line; std::getline(input, line);) {
      if (!line.empty() && line.front() != '#') ++count;
    }
    return count;
  };
  std::filesystem::create_directories(root);
  writeText(root / "dataset.info",
            "format=prism-dataset-v6\n"
            "complete=1\n"
            "recording_mode=imu-only\n"
            "image_storage=none\n"
            "camera_index=none\n"
            "lidar_storage=none\n"
            "lidar_imu_storage=none\n"
            "time_domain=rk-clock-realtime\n"
            "timestamp_epoch=unix\n"
            "alignment=common-device-time-domain\n"
            "imu0_samples=" + std::to_string(rows(imu0_rows)) + "\n" +
            "imu1_samples=" + std::to_string(rows(imu1_rows)) + "\n" +
            "lidar_batches=0\n"
            "lidar_points=0\n"
            "lidar_imu_samples=0\n");
  writeText(root / "imu0.tum", imu0_rows);
  writeText(root / "imu1.tum", imu1_rows);
}

bool hasIssue(const prism_viewer::dataset::DatasetValidationResult& result,
              prism_viewer::dataset::DatasetValidationSeverity severity,
              const std::string& needle) {
  for (const auto& issue : result.issues) {
    if (issue.severity == severity &&
        issue.message.find(needle) != std::string::npos) {
      return true;
    }
  }
  return false;
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

    std::filesystem::remove(root / "cam3.tum");

    const std::string normal_imu =
        "# imu\n"
        "1780000000.000000 0 0 9.8 0 0 0\n"
        "1780000000.001000 0 0 9.8 0 0 0\n"
        "1780000000.002000 0 0 9.8 0 0 0\n"
        "1780000000.003000 0 0 9.8 0 0 0\n";
    const auto valid_root = root / "valid";
    createImuOnlyDataset(valid_root, normal_imu, normal_imu);
    const auto valid = prism_viewer::dataset::validatePrismDataset(valid_root);
    if (!valid.valid || valid.warningCount() != 0u ||
        valid.onboard_imus[0].median_interval_us != 1000u) {
      throw std::runtime_error("valid IMU-only dataset was rejected");
    }

    const auto single_imu_root = root / "single-synced-imu";
    createImuOnlyDataset(single_imu_root, normal_imu,
                         "# IMU1 has no synchronized samples\n");
    const auto single_imu =
        prism_viewer::dataset::validatePrismDataset(single_imu_root);
    if (!single_imu.valid || single_imu.onboard_imus[0].rows != 4u ||
        single_imu.onboard_imus[1].rows != 0u) {
      throw std::runtime_error("single synchronized IMU dataset was rejected");
    }

    const auto empty_imus_root = root / "empty-imus";
    createImuOnlyDataset(empty_imus_root, "# empty IMU0\n",
                         "# empty IMU1\n");
    const auto empty_imus =
        prism_viewer::dataset::validatePrismDataset(empty_imus_root);
    if (empty_imus.valid ||
        !hasIssue(empty_imus,
                  prism_viewer::dataset::DatasetValidationSeverity::Error,
                  "all onboard IMU streams are empty")) {
      throw std::runtime_error("dataset with no synchronized IMU was accepted");
    }

    const auto repeat_root = root / "timestamp-repeat";
    createImuOnlyDataset(
        repeat_root,
        "1780000000.000000 0 0 9.8 0 0 0\n"
        "1780000000.001000 0 0 9.8 0 0 0\n"
        "1780000000.001000 0 0 9.8 0 0 0\n",
        normal_imu);
    const auto repeated =
        prism_viewer::dataset::validatePrismDataset(repeat_root);
    if (repeated.valid ||
        !hasIssue(repeated,
                  prism_viewer::dataset::DatasetValidationSeverity::Error,
                  "repeated")) {
      throw std::runtime_error("repeated timestamp was not rejected");
    }

    const auto jump_root = root / "timestamp-jump";
    createImuOnlyDataset(
        jump_root,
        "1780000000.000000 0 0 9.8 0 0 0\n"
        "1780000000.001000 0 0 9.8 0 0 0\n"
        "1780000000.002000 0 0 9.8 0 0 0\n"
        "1780000000.050000 0 0 9.8 0 0 0\n",
        normal_imu);
    const auto jumped =
        prism_viewer::dataset::validatePrismDataset(jump_root);
    if (!jumped.valid ||
        !hasIssue(jumped,
                  prism_viewer::dataset::DatasetValidationSeverity::Warning,
                  "jumped forward")) {
      throw std::runtime_error("timestamp discontinuity was not reported");
    }

    const auto large_jump_root = root / "timestamp-large-jump";
    createImuOnlyDataset(
        large_jump_root,
        "1780000000.000000 0 0 9.8 0 0 0\n"
        "1780000000.001000 0 0 9.8 0 0 0\n"
        "1780000000.002000 0 0 9.8 0 0 0\n"
        "1780000002.500000 0 0 9.8 0 0 0\n",
        normal_imu);
    const auto large_jump =
        prism_viewer::dataset::validatePrismDataset(large_jump_root);
    if (large_jump.valid ||
        !hasIssue(large_jump,
                  prism_viewer::dataset::DatasetValidationSeverity::Error,
                  "jumped forward")) {
      throw std::runtime_error("large timestamp jump was not rejected");
    }

    const auto camera_root = root / "bad-camera-container";
    std::filesystem::create_directories(camera_root);
    writeText(camera_root / "dataset.info",
              "format=prism-dataset-v6\ncomplete=1\n"
              "recording_mode=full\nimage_storage=chunk-v1\n"
              "camera_index=chunk-v2-with-actual-exposure\n"
              "lidar_storage=none\nlidar_imu_storage=none\n"
              "time_domain=rk-clock-realtime\ntimestamp_epoch=unix\n"
              "alignment=common-device-time-domain\n"
              "camera0_images=1\ncamera1_images=1\n"
              "camera2_images=1\ncamera3_images=1\n"
              "imu0_samples=4\nimu1_samples=4\n"
              "lidar_batches=0\nlidar_points=0\n"
              "lidar_imu_samples=0\n");
    writeBytes(camera_root / "camera-data-0000.bin",
               {0xffu, 0xd8u, 0xffu, 0xd9u});
    for (size_t camera = 0; camera < 4u; ++camera) {
      writeText(camera_root / ("cam" + std::to_string(camera) + ".tum"),
                "1780000000.000000 camera-data-0000.bin 0 100 1000\n");
    }
    writeText(camera_root / "imu0.tum", normal_imu);
    writeText(camera_root / "imu1.tum", normal_imu);
    const auto bad_container =
        prism_viewer::dataset::validatePrismDataset(camera_root);
    if (bad_container.valid ||
        !hasIssue(bad_container,
                  prism_viewer::dataset::DatasetValidationSeverity::Error,
                  "exceeds")) {
      throw std::runtime_error("container bounds error was not rejected");
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
