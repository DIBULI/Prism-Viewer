#include "dataset/dataset_browser.hpp"
#include <QBuffer>
#include <QCoreApplication>
#include <QImage>

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
                          const std::string& imu1_rows,
                          const std::string& time_domain =
                              "rk-clock-realtime",
                          const std::string& timestamp_epoch = "unix") {
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
            "time_domain=" + time_domain + "\n" +
            "timestamp_epoch=" + timestamp_epoch + "\n" +
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

void changeField(const std::filesystem::path& root, const std::string& before,
                 const std::string& after) {
  std::ifstream input(root / "dataset.info");
  std::string text((std::istreambuf_iterator<char>(input)), {});
  const auto at = text.find(before);
  if (at == std::string::npos) throw std::runtime_error("fixture field missing");
  text.replace(at, before.size(), after);
  writeText(root / "dataset.info", text);
}

const std::string time_sync_header =
    "sample_index,recording_elapsed_us,host_receive_unix_us,rk_system_time_us,"
    "device_info_version,sensor_board_online,sensor_board_time_synced,"
    "time_sync_provider,time_sync_provider_name,imu_time_synced_mask,"
    "sensor_board_error_flags\n";

void createReceivedDataset(const std::filesystem::path& root,
                           bool lidar_samples = false) {
  std::filesystem::create_directories(root);
  QImage image(8, 8, QImage::Format_RGB32);
  image.fill(Qt::green);
  QByteArray jpeg;
  QBuffer buffer(&jpeg);
  buffer.open(QIODevice::WriteOnly);
  if (!image.save(&buffer, "JPEG")) throw std::runtime_error("JPEG fixture");
  std::vector<uint8_t> payload;
  const std::array<unsigned, 4> counts{2, 2, 0, 1};
  std::string info =
      "format=prism-dataset-v6\ndevice_model=413GSV0\ncomplete=1\n"
      "recording_mode=full\nimage_storage=chunk-v1\n"
      "camera_index=chunk-v2-with-actual-exposure\n"
      "time_domain=sensor-board-clock\ntimestamp_epoch=boot\n"
      "recording_stream_policy=all-received\n"
      "timestamp_policy=source-preserved-see-per-stream-flags\n"
      "alignment=not-asserted\nimu0_samples=0\nimu1_samples=0\n"
      "lidar_storage=cartesian-mm-chunk-v3-with-point-time\n"
      "lidar_imu_storage=tum-si-v2-with-time-source\n"
      "time_sync_storage=csv-v1\ntime_sync_samples=0\n"
      "time_sync_provider_transitions=0\n";
  for (unsigned c = 0; c < 4; ++c) {
    std::ostringstream index;
    index << "# recorded image index\n";
    for (unsigned f = 0; f < counts[c]; ++f) {
      index << (c == 1 ? "2." : "1.") << (f ? "033000" : "000000")
            << " camera.bin " << payload.size() << ' ' << jpeg.size() << " 1000\n";
      payload.insert(payload.end(), jpeg.begin(), jpeg.end());
    }
    writeText(root / ("cam" + std::to_string(c) + ".tum"), index.str());
    info += "camera" + std::to_string(c) + "_images=" + std::to_string(counts[c]) + "\n";
  }
  writeBytes(root / "camera.bin", payload);
  writeText(root / "imu0.tum", "# no samples received\n");
  writeText(root / "imu1.tum", "# no samples received\n");
  writeText(root / "time_sync.csv", time_sync_header);
  if (lidar_samples) {
    writeBytes(root / "points.bin", std::vector<uint8_t>(16, 0));
    writeText(root / "lidar.tum", "3.000000 points.bin 0 16 1 1 8 0 1 3000000 100 0 0\n");
    writeText(root / "lidar_imu.tum", "3.000000 0 0 9.8 0 0 0 1 8 0 1 3000000 0 0\n");
  } else {
    writeText(root / "lidar.tum", "# no point batches received\n");
    writeText(root / "lidar_imu.tum", "# no LiDAR IMU received\n");
  }
  info += std::string("lidar_batches=") + (lidar_samples ? "1" : "0") +
      "\nlidar_points=" + (lidar_samples ? "1" : "0") +
      "\nlidar_imu_samples=" + (lidar_samples ? "1" : "0") + "\n";
  writeText(root / "dataset.info", info);
}

void testReceivedPolicy(const std::filesystem::path& base) {
  using namespace prism_viewer::dataset;
  const auto verify = [&](const char* name, bool expected, const char* message = "") {
    const auto result = validatePrismDataset(base / name);
    if (result.valid != expected || (expected && result.warningCount() == 0) ||
        (*message && !hasIssue(result, expected ? DatasetValidationSeverity::Warning :
                              DatasetValidationSeverity::Error, message))) {
      std::ostringstream reason;
      reason << name << ": unexpected validity " << result.valid;
      for (const auto& i : result.issues) reason << "\n" << i.file << ": " << i.message;
      throw std::runtime_error(reason.str());
    }
  };
  auto p = base / "optional";
  createReceivedDataset(p);
  verify("optional", true, "all onboard IMU streams are empty");
  verify("optional", true, "timestamps are not identical");

  p = base / "unsynced";
  createReceivedDataset(p, true);
  verify("unsynced", true, "LiDAR records are explicitly marked unsynchronized");
  verify("unsynced", true, "LiDAR IMU records are explicitly marked unsynchronized");

  p = base / "unix-with-relative-lidar";
  createReceivedDataset(p, true);
  changeField(p, "time_domain=sensor-board-clock", "time_domain=rk-clock-realtime");
  changeField(p, "timestamp_epoch=boot", "timestamp_epoch=unix");
  for (unsigned c = 0; c < 4; ++c) {
    const auto path = p / ("cam" + std::to_string(c) + ".tum");
    std::ifstream input(path);
    std::string rows, line;
    while (std::getline(input, line))
      rows += (line.empty() || line.front() == '#' ? line : "178000000" + line) + "\n";
    input.close();
    writeText(path, rows);
  }
  verify("unix-with-relative-lidar", true, "explicitly marked unsynchronized");
  writeText(p / "lidar.tum", "3.000000 points.bin 0 16 1 1 8 0 1 3000000 100 1 0\n");
  verify("unix-with-relative-lidar", false, "outside the declared RK CLOCK_REALTIME epoch");

  p = base / "strict";
  createReceivedDataset(p, true);
  changeField(p, "alignment=not-asserted", "alignment=common-device-time-domain");
  verify("strict", false, "timestamp is not synchronized");

  p = base / "partial-policy";
  createReceivedDataset(p);
  changeField(p, "timestamp_policy=source-preserved-see-per-stream-flags",
                 "timestamp_policy=unknown");
  verify("partial-policy", false, "time-domain declaration");

  p = base / "future-domain";
  createReceivedDataset(p);
  changeField(p, "time_domain=sensor-board-clock", "time_domain=unknown");
  verify("future-domain", false, "time-domain declaration");

  p = base / "incomplete";
  createReceivedDataset(p);
  changeField(p, "complete=1", "complete=0");
  verify("incomplete", false, "recording is incomplete");

  p = base / "bad-count";
  createReceivedDataset(p);
  changeField(p, "camera0_images=2", "camera0_images=9");
  verify("bad-count", false, "declares 9");

  p = base / "bad-lidar-count";
  createReceivedDataset(p);
  changeField(p, "lidar_batches=0", "lidar_batches=1");
  verify("bad-lidar-count", false, "declares 1");

  p = base / "bounds";
  createReceivedDataset(p);
  writeBytes(p / "camera.bin", {0});
  verify("bounds", false, "exceeds");

  p = base / "bad-jpeg";
  createReceivedDataset(p);
  writeBytes(p / "camera.bin", std::vector<uint8_t>(
      std::filesystem::file_size(p / "camera.bin"), 0));
  verify("bad-jpeg", false, "decodable JPEG");

  p = base / "missing-index";
  createReceivedDataset(p);
  std::filesystem::remove(p / "cam2.tum");
  verify("missing-index", false, "camera indexes are incomplete");

  p = base / "bad-flags";
  createReceivedDataset(p, true);
  writeText(p / "lidar.tum", "3.000000 points.bin 0 16 1 1 8 0 1 3000000 100 2 0\n");
  verify("bad-flags", false, "invalid LiDAR time-source fields");

  p = base / "missing-flags";
  createReceivedDataset(p, true);
  writeText(p / "lidar.tum", "3.000000 points.bin 0 16 1 1 8 0 1 3000000\n");
  writeText(p / "lidar_imu.tum", "3.000000 0 0 9.8 0 0 0\n");
  verify("missing-flags", false, "time-source fields are missing");

  p = base / "clock-status";
  createReceivedDataset(p);
  changeField(p, "time_sync_samples=0", "time_sync_samples=1");
  writeText(p / "time_sync.csv", time_sync_header +
      "0,0,1780000000000000,0,3,1,0,0,Sensor Board internal,0,0\n");
  verify("clock-status", true, "status snapshots");
  writeText(p / "time_sync.csv", time_sync_header +
      "0,0,1780000000000000,0,3,0,1,0,Sensor Board internal,0,0\n");
  verify("clock-status", false, "time-sync sequence");

  p = base / "point-only";
  createReceivedDataset(p, true);
  changeField(p, "lidar_imu_storage=tum-si-v2-with-time-source", "lidar_imu_storage=none");
  changeField(p, "lidar_imu_samples=1", "lidar_imu_samples=0");
  std::filesystem::remove(p / "lidar_imu.tum");
  verify("point-only", true);

  p = base / "empty";
  createReceivedDataset(p);
  for (unsigned c = 0; c < 4; ++c) {
    writeText(p / ("cam" + std::to_string(c) + ".tum"), "# empty\n");
    const auto before = c < 2 ? "2" : c == 2 ? "0" : "1";
    changeField(p, "camera" + std::to_string(c) + "_images=" + before,
                   "camera" + std::to_string(c) + "_images=0");
  }
  verify("empty", false, "contains no sensor records");
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  const auto nonce =
      std::chrono::steady_clock::now().time_since_epoch().count();
  const std::filesystem::path root =
      std::filesystem::temp_directory_path() /
      ("prism-dataset-browser-test-" + std::to_string(nonce));
  std::filesystem::create_directories(root);
  try {
    testReceivedPolicy(root / "received-policy");
    const auto models = root / "model-cases";
    std::filesystem::create_directories(models);
    const auto identity = [&](const std::string& manifest, const char* model,
                              bool defaulted, bool raw = false) {
      writeText(models / "dataset.info", manifest);
      const auto info = prism_viewer::dataset::readDatasetIdentity(models);
      if (info.model != model || info.defaulted != defaulted || info.raw8_recording != raw)
        throw std::runtime_error("dataset identity mismatch");
    };
    if (prism_viewer::dataset::readDatasetIdentity(models).model != "413GSV0")
      throw std::runtime_error("missing manifest must default to 413GSV0");
    identity("format=prism-dataset-v6\n", "413GSV0", true);
    identity("device_model=\n", "413GSV0", true);
    identity("device_model=A4L\n", "413GSV0", false);
    identity("device_model=413GSV0\n", "413GSV0", false);
    identity("device_model=41M3\n", "413GSV0", false);
    identity("device_model=Prism-41m3\nmodel=413GSV0\n", "413GSV0", false);
    identity("device_model=1315\nmodel=21312V0\n", "21312V0", false);
    identity(R"({"device_model":"Prism-1315","model":"21312V0"})", "21312V0", false);
    identity("device_model=Prism-413gsv0\n", "413GSV0", false);
    identity("device_model=413GSV0\nmodel=Prism-A4L\n", "413GSV0", false);
    identity(R"({"model":"Prism-A4L"})", "413GSV0", false);
    identity("\xef\xbb\xbf" "device_model=415RSV0\n", "415RSV0", false);
    identity("device_model=415RSV0\n", "415RSV0", false);
    identity(" # comment\r\n device_model = Prism-415RSV0 \r\n", "415RSV0", false);
    identity("device_model=NEXT-1\n", "NEXT-1", false);
    identity("device_model=415RSV0\nmodel=Prism-415RSV0\ncodec=raw8-rice-lossless\n", "415RSV0", false, true);
    identity("codec=raw8-rice-lossless\n", "413GSV0", true, true);
    identity(R"({"model":"Prism-415RSV0","codec":"raw8-rice-lossless"})", "415RSV0", false, true);
    identity(R"({"schema":1})", "413GSV0", true);
    for (const auto& bad : {"device_model=A4L\ndevice_model=415RSV0\n",
                           "device_model=A4L\nmodel=415RSV0\n", "device_model=<script>\n",
                           "{\"device_model\":42}", "{invalid}"}) {
      writeText(models / "dataset.info", bad);
      bool rejected=false;
      try { prism_viewer::dataset::readDatasetIdentity(models); }
      catch (const std::exception&) { rejected=true; }
      if (!rejected) throw std::runtime_error("malformed identity accepted");
    }
    identity("device_model=415RSV0\ncodec=raw8-rice-lossless\n", "415RSV0", false, true);
    const auto raw = prism_viewer::dataset::validatePrismDataset(models);
    if (raw.valid || raw.device_model != "415RSV0" || raw.errorCount() != 1)
      throw std::runtime_error("model recognition claimed unsupported RAW8 validation");

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
    if (!valid.valid || valid.warningCount() != 0u || valid.device_model != "413GSV0" ||
        valid.onboard_imus[0].median_interval_us != 1000u) {
      throw std::runtime_error("valid IMU-only dataset was rejected");
    }

    const std::string boot_time_imu =
        "# sensor-board boot timeline\n"
        "1.000000 0 0 9.8 0 0 0\n"
        "1.001000 0 0 9.8 0 0 0\n";
    const auto boot_time_root = root / "sensor-board-boot-time";
    createImuOnlyDataset(boot_time_root, boot_time_imu,
                         "# empty IMU1\n", "sensor-board-clock", "boot");
    const auto boot_time =
        prism_viewer::dataset::validatePrismDataset(boot_time_root);
    if (!boot_time.valid ||
        boot_time.time_domain != "sensor-board-clock" ||
        boot_time.timestamp_epoch != "boot" ||
        boot_time.onboard_imus[0].first_timestamp_us != 1000000u) {
      throw std::runtime_error(
          "valid Sensor Board boot-time dataset was rejected");
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
