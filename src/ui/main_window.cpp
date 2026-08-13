#include "communication/prism_runtime.hpp"
#include "common/ui_text.hpp"
#include "communication/device_session.hpp"
#include "control/operation_controller.hpp"
#include "dataset/dataset_browser.hpp"
#include "dataset/imu_time_alignment.hpp"
#include "dataset/rosbag_exporter.hpp"
#include "imu_units.hpp"
#include "imu_timestamp_policy.hpp"
#include "transfer/camera_frame_assembler.hpp"
#include "ui/app_theme.hpp"
#include "ui/camera_encoding_panel.hpp"
#include "ui/camera_exposure_panel.hpp"
#include "ui/camera_zoom_dialog.hpp"
#include "ui/device_info_panel.hpp"
#include "ui/image_view_label.hpp"
#include "ui/lidar_point_cloud_widget.hpp"
#include "ui/main_window.hpp"
#include "ui/preview_image_decoder.hpp"
#include "ui/wifi_hotspot_panel.hpp"

#include <QtCore/QByteArray>
#include <QtCore/QBuffer>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QEvent>
#include <QtCore/QFileInfo>
#include <QtCore/QMetaObject>
#include <QtCore/QLocale>
#include <QtCore/QProcess>
#include <QtCore/QSaveFile>
#include <QtCore/QSettings>
#include <QtCore/QSignalBlocker>
#include <QtCore/QStringList>
#include <QtCore/QTimer>
#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QLegendMarker>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QtGui/QAction>
#else
#include <QtWidgets/QAction>
#endif
#include <QtGui/QBrush>
#include <QtGui/QCloseEvent>
#include <QtGui/QClipboard>
#include <QtGui/QColor>
#include <QtGui/QImage>
#include <QtGui/QIcon>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtGui/QPixmap>
#include <QtWidgets/QAbstractItemView>
#include <QtWidgets/QApplication>
#include <QtWidgets/QButtonGroup>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QFrame>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QMenu>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QProgressDialog>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QScrollBar>
#include <QtWidgets/QSlider>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QSplitter>
#include <QtWidgets/QTableWidget>
#include <QtWidgets/QTableWidgetItem>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
QT_CHARTS_USE_NAMESPACE
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <locale>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr auto kImuRateWindow = std::chrono::seconds(5);
constexpr auto kCameraRateWindow = std::chrono::seconds(2);
constexpr auto kCameraFrameSetProgressTimeout = std::chrono::seconds(5);
constexpr auto kCameraControlCommandFreshnessLimit =
    std::chrono::seconds(1);
// Keep transport processing at the native IMU rate. The table receives the
// latest value at 50 Hz, while the chart keeps a separate bounded 100 Hz
// sample stream and repaints at about 30 fps. This preserves short motion
// details without making QtCharts redraw at the 500/1000 Hz sensor rate.
constexpr auto kImuUiPeriod = std::chrono::milliseconds(20);
constexpr auto kImuUiFlushPeriod = std::chrono::milliseconds(20);
constexpr auto kImuPlotSamplePeriod = std::chrono::milliseconds(10);
constexpr auto kImuPlotRefreshPeriod = std::chrono::milliseconds(33);
constexpr size_t kMaximumPendingImuPlotSamples = 32;
constexpr auto kMetadataUiPeriod = std::chrono::milliseconds(200);
constexpr auto kCameraStatusUiPeriod = std::chrono::milliseconds(250);
constexpr size_t kMaximumQueuedPreviewFrameSets = 1;
constexpr int kCameraPreviewWidth = 640;
constexpr int kCameraPreviewHeight = 512;
// Keep the top-level minimum below a common 1600x900 desktop. Individual tab
// pages may contain larger controls, but hidden pages must not prevent the
// window manager from honoring maximize on the active screen.
constexpr int kMaximumMainWindowMinimumWidth = 1600;
constexpr int kMaximumMainWindowMinimumHeight = 900;
using prism_viewer::common::fromFilesystemPath;
using prism_viewer::common::toFilesystemPath;
using prism_viewer::common::toQString;
using prism_viewer::common::uiText;
using prism_viewer::common::wideToQString;
using prism_viewer::dataset::DatasetImageEntry;
using prism_viewer::dataset::DatasetTimestampSummary;
using prism_viewer::dataset::DatasetValidationResult;
using prism_viewer::dataset::DatasetValidationSeverity;
using prism_viewer::dataset::ImuTimeAlignmentResult;
using prism_viewer::dataset::ImuTimeAlignmentStatus;
using prism_viewer::dataset::RosbagFormat;
using prism_viewer::dataset::analyzeImu0LidarImuTimeOffset;
using prism_viewer::dataset::validatePrismDataset;
using prism_viewer::dataset::TumFileSummary;
using prism_viewer::dataset::inspectDatasetCameraIndexes;
using prism_viewer::dataset::loadDatasetImage;
using prism_viewer::dataset::loadDatasetImageIndex;
using prism_viewer::dataset::summarizeTumFile;
using prism_viewer::imu_units::AccelerationUnit;
using prism_viewer::imu_units::AngularVelocityUnit;
using prism_viewer::imu_units::TemperatureUnit;
using prism_viewer::imu_units::convertAcceleration;
using prism_viewer::imu_units::convertAngularVelocity;
using prism_viewer::imu_units::convertTemperature;
using prism_viewer::ui::CameraEncodingPanel;
using prism_viewer::ui::CameraExposurePanel;
using prism_viewer::ui::CameraZoomDialog;
using prism_viewer::ui::DeviceInfoPanel;
using prism_viewer::ui::WifiHotspotPanel;
using prism_viewer::ui::WifiHotspotViewState;
using prism_viewer::ui::decodePreviewJpeg;

QString accelerationUnitText(AccelerationUnit unit) {
  switch (unit) {
    case AccelerationUnit::MilliGravity:
      return QStringLiteral("mg");
    case AccelerationUnit::Gravity:
      return QStringLiteral("g");
    case AccelerationUnit::MetresPerSecondSquared:
      return QStringLiteral("m/s") + QChar(0x00b2);
  }
  return QStringLiteral("g");
}

QString angularVelocityUnitText(AngularVelocityUnit unit) {
  switch (unit) {
    case AngularVelocityUnit::MilliDegreesPerSecond:
      return QStringLiteral("mdps");
    case AngularVelocityUnit::DegreesPerSecond:
      return QChar(0x00b0) + QStringLiteral("/s");
    case AngularVelocityUnit::RadiansPerSecond:
      return QStringLiteral("rad/s");
  }
  return QChar(0x00b0) + QStringLiteral("/s");
}

QString temperatureUnitText(TemperatureUnit unit) {
  switch (unit) {
    case TemperatureUnit::MilliCelsius:
      return QStringLiteral("m") + QChar(0x00b0) + QStringLiteral("C");
    case TemperatureUnit::Celsius:
      return QChar(0x00b0) + QStringLiteral("C");
  }
  return QChar(0x00b0) + QStringLiteral("C");
}

QString unitTokenText(AccelerationUnit unit) {
  const std::string_view value = prism_viewer::imu_units::token(unit);
  return QString::fromLatin1(value.data(), static_cast<int>(value.size()));
}

QString unitTokenText(AngularVelocityUnit unit) {
  const std::string_view value = prism_viewer::imu_units::token(unit);
  return QString::fromLatin1(value.data(), static_cast<int>(value.size()));
}

QString unitTokenText(TemperatureUnit unit) {
  const std::string_view value = prism_viewer::imu_units::token(unit);
  return QString::fromLatin1(value.data(), static_cast<int>(value.size()));
}

int accelerationDisplayPrecision(AccelerationUnit unit) {
  return unit == AccelerationUnit::MilliGravity ? 0 : 4;
}

int angularVelocityDisplayPrecision(AngularVelocityUnit unit) {
  switch (unit) {
    case AngularVelocityUnit::MilliDegreesPerSecond:
      return 0;
    case AngularVelocityUnit::DegreesPerSecond:
      return 3;
    case AngularVelocityUnit::RadiansPerSecond:
      return 5;
  }
  return 3;
}

int temperatureDisplayPrecision(TemperatureUnit unit) {
  return unit == TemperatureUnit::MilliCelsius ? 0 : 2;
}

struct CameraPreviewJob {
  uint32_t frame_id = 0;
  uint64_t received_frame_sets = 0;
  uint64_t generation = 0;
  int full_resolution_camera = -1;
  QSize maximum_preview_size{kCameraPreviewWidth, kCameraPreviewHeight};
  std::array<std::vector<uint8_t>, 4> jpeg;
};

struct DecodedCameraPreviewJob {
  uint32_t frame_id = 0;
  uint64_t received_frame_sets = 0;
  uint64_t generation = 0;
  bool decode_ok = true;
  size_t failed_camera = 0;
  std::array<QImage, 4> images;
};

struct ImuUiSnapshot {
  prism::ImuSample sample;
  uint64_t received_count = 0;
  double sample_rate_hz = 0.0;
  uint64_t fsync_event_count = 0;
  uint64_t last_fsync_sample_us = 0;
  bool last_fsync_delay_valid = false;
};

struct PendingImuPlotSample {
  prism::ImuSample sample;
  std::chrono::steady_clock::time_point received_at;
};

class SampleRateTracker {
 public:
  void add(std::chrono::steady_clock::time_point now) {
    ++total_samples_;
    if (anchors_.empty()) {
      anchors_.push_back({now, total_samples_});
      next_anchor_ = now + kAnchorPeriod;
      return;
    }
    if (now < next_anchor_) return;

    anchors_.push_back({now, total_samples_});
    next_anchor_ = now + kAnchorPeriod;
    while (anchors_.size() > 2 &&
           now - anchors_[1].time >= kImuRateWindow) {
      anchors_.pop_front();
    }
  }

  double rate(std::chrono::steady_clock::time_point now) const {
    if (anchors_.empty()) return 0.0;
    const double elapsed =
        std::chrono::duration<double>(now - anchors_.front().time).count();
    if (elapsed <= 0.0) return 0.0;
    return static_cast<double>(total_samples_ - anchors_.front().sample_count) /
           elapsed;
  }

 private:
  struct Anchor {
    std::chrono::steady_clock::time_point time;
    uint64_t sample_count = 0;
  };

  static constexpr auto kAnchorPeriod = std::chrono::milliseconds(100);
  std::deque<Anchor> anchors_;
  std::chrono::steady_clock::time_point next_anchor_{};
  uint64_t total_samples_ = 0;
};

using prism_viewer::ui::ImageViewLabel;

enum class DatasetRecordingMode {
  Full,
  ImuOnly,
};

bool shouldTakeFrameJob(bool frame_available, bool lidar_available,
                        uint64_t frame_timestamp_us,
                        uint64_t lidar_timestamp_us) {
  return frame_available &&
         (!lidar_available || frame_timestamp_us <= lidar_timestamp_us);
}

struct DatasetRecordingSummary {
  DatasetRecordingMode mode = DatasetRecordingMode::Full;
  bool had_session = false;
  bool success = true;
  std::array<uint64_t, 2> sample_count{};
  std::array<uint64_t, 4> image_count{};
  uint64_t dropped_frame_sets = 0;
  uint64_t lidar_batch_count = 0;
  uint64_t lidar_point_count = 0;
  uint64_t dropped_lidar_batches = 0;
  uint64_t dropped_lidar_points = 0;
  uint64_t lidar_imu_sample_count = 0;
  std::array<uint64_t, 2> unsynced_imu_samples_dropped{};
  uint64_t unsynced_camera_frame_sets_dropped = 0;
  uint64_t unsynced_lidar_batches_dropped = 0;
  uint64_t unsynced_lidar_points_dropped = 0;
  uint64_t unsynced_lidar_imu_samples_dropped = 0;
  std::string error;

  uint64_t unsyncedDropCount() const {
    return unsynced_imu_samples_dropped[0] +
           unsynced_imu_samples_dropped[1] +
           unsynced_camera_frame_sets_dropped +
           unsynced_lidar_batches_dropped +
           unsynced_lidar_imu_samples_dropped;
  }
};

// Records either a complete dataset or IMU-only data. imu0/imu1 always mean
// the two onboard sensors; an enabled Livox IMU is written independently as
// lidar_imu.tum. In full mode, image and point-cloud payloads are appended to
// sequential containers so exFAT does not allocate a 256-KiB cluster for every
// message. The large-payload writer stays off the USB receive thread.
class DatasetRecorder {
 public:
  bool start(const std::filesystem::path& root, bool overwrite,
             DatasetRecordingMode mode, bool record_lidar_streams,
             std::string* error) {
    std::unique_lock<std::mutex> lock(mutex_);
    try {
      if (session_open_) {
        if (error != nullptr) *error = "a dataset recording is already active";
        return false;
      }

      std::error_code filesystem_error;
      std::filesystem::create_directories(root, filesystem_error);
      if (filesystem_error) {
        if (error != nullptr) *error = "cannot create the dataset directory";
        return false;
      }
      root_ = root;
      const std::array<std::filesystem::path, 9> known_outputs = {
          root_ / "imu0.tum", root_ / "imu1.tum", root_ / "cam0.tum",
          root_ / "cam1.tum", root_ / "cam2.tum", root_ / "cam3.tum",
          root_ / "lidar.tum", root_ / "lidar_imu.tum",
          root_ / "dataset.info"};
      bool existing_dataset = false;
      for (const auto& path : known_outputs) {
        const bool exists = std::filesystem::exists(path, filesystem_error);
        if (filesystem_error) {
          if (error != nullptr) {
            *error = "cannot inspect the dataset directory";
          }
          return false;
        }
        existing_dataset = existing_dataset || exists;
      }
      for (size_t camera = 0; camera < 4; ++camera) {
        const bool exists = std::filesystem::exists(
            root_ / ("cam" + std::to_string(camera)), filesystem_error);
        if (filesystem_error) {
          if (error != nullptr) {
            *error = "cannot inspect the dataset directory";
          }
          return false;
        }
        existing_dataset = existing_dataset || exists;
      }
      std::vector<std::filesystem::path> old_data_containers;
      std::filesystem::directory_iterator iterator(root_, filesystem_error);
      const std::filesystem::directory_iterator end;
      if (filesystem_error) {
        if (error != nullptr) {
          *error = "cannot inspect the dataset directory";
        }
        return false;
      }
      while (iterator != end) {
        if (isDatasetDataContainer(iterator->path())) {
          existing_dataset = true;
          old_data_containers.push_back(iterator->path());
        }
        iterator.increment(filesystem_error);
        if (filesystem_error) {
          if (error != nullptr) {
            *error = "cannot inspect all entries in the dataset directory";
          }
          return false;
        }
      }
      if (existing_dataset && !overwrite) {
        if (error != nullptr) *error = "dataset files already exist";
        return false;
      }
      if (overwrite) {
        for (const auto& path : known_outputs) {
          std::filesystem::remove(path, filesystem_error);
          if (filesystem_error) {
            if (error != nullptr) {
              *error = "cannot replace an old dataset index";
            }
            return false;
          }
        }
        for (size_t camera = 0; camera < 4; ++camera) {
          std::filesystem::remove_all(
              root_ / ("cam" + std::to_string(camera)), filesystem_error);
          if (filesystem_error) {
            if (error != nullptr) {
              *error = "cannot replace an old camera directory";
            }
            return false;
          }
        }
        for (const auto& path : old_data_containers) {
          std::filesystem::remove(path, filesystem_error);
          if (filesystem_error) {
            if (error != nullptr) {
              *error = "cannot replace an old dataset data container";
            }
            return false;
          }
        }
      }

      imu_counts_.fill(0);
      image_counts_.fill(0);
      dropped_frame_sets_ = 0;
      lidar_batch_count_ = 0;
      lidar_point_count_ = 0;
      dropped_lidar_batches_ = 0;
      dropped_lidar_points_ = 0;
      lidar_imu_sample_count_ = 0;
      unsynced_imu_samples_dropped_.fill(0);
      unsynced_camera_frame_sets_dropped_ = 0;
      unsynced_lidar_batches_dropped_ = 0;
      unsynced_lidar_points_dropped_ = 0;
      unsynced_lidar_imu_samples_dropped_ = 0;
      write_failed_ = false;
      write_error_.clear();
      frame_jobs_.clear();
      lidar_jobs_.clear();
      queued_payload_bytes_ = 0;
      stop_writer_ = false;
      start_unix_us_ = wallClockUs();
      camera_chunk_index_ = 0;
      camera_chunk_size_ = 0;
      camera_chunk_name_.clear();
      lidar_chunk_index_ = 0;
      lidar_chunk_size_ = 0;
      lidar_chunk_name_.clear();
      mode_.store(mode, std::memory_order_relaxed);
      record_lidar_streams_.store(record_lidar_streams,
                                  std::memory_order_relaxed);

      for (size_t sensor = 0; sensor < imu_files_.size(); ++sensor) {
        imu_files_[sensor].open(
            root_ / ("imu" + std::to_string(sensor) + ".tum"),
            std::ios::out | std::ios::trunc);
        imu_files_[sensor].imbue(std::locale::classic());
        if (!imu_files_[sensor].is_open()) {
          for (auto& file : imu_files_) {
            if (file.is_open()) file.close();
          }
          if (error != nullptr) {
            *error = "cannot open output file for IMU" +
                     std::to_string(sensor);
          }
          return false;
        }
        imu_files_[sensor]
            << "# Prism TUM-style IMU stream\n"
            << "# timestamp[s] ax[m/s^2] ay[m/s^2] az[m/s^2] "
               "gx[rad/s] gy[rad/s] gz[rad/s]\n";
      }
      if (mode == DatasetRecordingMode::Full) {
        for (size_t camera = 0; camera < camera_index_files_.size(); ++camera) {
          camera_index_files_[camera].open(
              root_ / ("cam" + std::to_string(camera) + ".tum"),
              std::ios::out | std::ios::trunc);
          camera_index_files_[camera].imbue(std::locale::classic());
          if (!camera_index_files_[camera].is_open()) {
            closeFiles();
            if (error != nullptr) *error = "cannot open camera index file";
            return false;
          }
          camera_index_files_[camera]
              << "# Prism TUM-style image stream\n"
              << "# timestamp[s] container_path byte_offset byte_size "
                 "actual_exposure_us\n";
        }
        if (record_lidar_streams) {
          lidar_index_file_.open(root_ / "lidar.tum",
                                 std::ios::out | std::ios::trunc);
          lidar_index_file_.imbue(std::locale::classic());
          if (!lidar_index_file_.is_open()) {
            closeFiles();
            if (error != nullptr) *error = "cannot open LiDAR index file";
            return false;
          }
          lidar_index_file_
              << "# Prism Livox point-batch stream\n"
              << "# timestamp[s] container_path byte_offset byte_size "
                 "point_count model device_type time_type batch_id "
                 "timestamp_raw time_interval_100ns timestamp_synced "
                 "tai_offset_applied\n";
        }
      }
      if (record_lidar_streams) {
        lidar_imu_file_.open(root_ / "lidar_imu.tum",
                             std::ios::out | std::ios::trunc);
        lidar_imu_file_.imbue(std::locale::classic());
        if (!lidar_imu_file_.is_open()) {
          closeFiles();
          if (error != nullptr) *error = "cannot open LiDAR IMU output file";
          return false;
        }
        lidar_imu_file_
            << "# Prism Livox IMU stream (separate from onboard imu0/imu1)\n"
            << "# timestamp[s] ax[m/s^2] ay[m/s^2] az[m/s^2] "
               "gx[rad/s] gy[rad/s] gz[rad/s] model device_type "
               "time_type sample_id timestamp_raw timestamp_synced "
               "tai_offset_applied\n";
      }

      writeManifest(false);
      if (write_failed_) {
        closeFiles();
        if (error != nullptr) {
          *error = write_error_.empty()
                       ? "cannot write in-progress dataset manifest"
                       : write_error_;
        }
        return false;
      }

      session_open_ = true;
      active_.store(true, std::memory_order_release);
      if (mode == DatasetRecordingMode::Full) {
        writer_ = std::thread([this]() { writerLoop(); });
      }
      return true;
    } catch (const std::exception& exception) {
      active_.store(false, std::memory_order_release);
      stop_writer_ = true;
      session_open_ = false;
      frame_jobs_.clear();
      lidar_jobs_.clear();
      queued_payload_bytes_ = 0;
      closeFiles();
      if (error != nullptr) {
        try {
          *error = std::string("dataset initialization failed: ") +
                   exception.what();
        } catch (...) {
          error->clear();
        }
      }
      return false;
    } catch (...) {
      active_.store(false, std::memory_order_release);
      stop_writer_ = true;
      session_open_ = false;
      frame_jobs_.clear();
      lidar_jobs_.clear();
      queued_payload_bytes_ = 0;
      closeFiles();
      if (error != nullptr) {
        try {
          *error = "dataset initialization failed";
        } catch (...) {
          error->clear();
        }
      }
      return false;
    }
  }

  void appendImu(const prism::ImuSample& sample) {
    try {
      if (!active_.load(std::memory_order_acquire) || sample.sensor_id >= 2) {
        return;
      }
      std::lock_guard<std::mutex> lock(mutex_);
      if (!active_.load(std::memory_order_relaxed) || !session_open_ ||
          write_failed_) {
        return;
      }
      if (!sample.timestamp_synced ||
          !isPlausibleRkClockRealtimeUs(sample.timestamp_us)) {
        ++unsynced_imu_samples_dropped_[sample.sensor_id];
        return;
      }

      constexpr double kStandardGravity = 9.80665;
      constexpr double kRadiansPerDegree =
          3.14159265358979323846 / 180.0;
      const uint64_t seconds = sample.timestamp_us / 1000000ULL;
      const uint64_t microseconds = sample.timestamp_us % 1000000ULL;
      auto& file = imu_files_[sample.sensor_id];
      file << seconds << '.' << std::setw(6) << std::setfill('0')
           << microseconds << std::setfill(' ') << std::fixed
           << std::setprecision(9);
      for (size_t axis = 0; axis < 3; ++axis) {
        file << ' ' << static_cast<double>(sample.accel_mg[axis]) *
                             kStandardGravity / 1000.0;
      }
      for (size_t axis = 0; axis < 3; ++axis) {
        file << ' ' << static_cast<double>(sample.gyro_mdps[axis]) *
                             kRadiansPerDegree / 1000.0;
      }
      file << '\n';
      if (!file.good()) {
        write_failed_ = true;
        write_error_ = "write failed (disk full or output path unavailable)";
        return;
      }
      ++imu_counts_[sample.sensor_id];
    } catch (...) {
      markWriteFailedNoThrow("IMU dataset write failed");
    }
  }

  void appendFrameSet(
      uint32_t frame_id, uint64_t timestamp_us,
      const prism::VideoMeta& metadata,
      const std::array<std::vector<uint8_t>, 4>& jpeg_set) {
    try {
      if (!active_.load(std::memory_order_acquire) ||
          mode_.load(std::memory_order_relaxed) !=
              DatasetRecordingMode::Full) {
        return;
      }
      uint64_t frame_set_bytes = 0;
      for (const auto& jpeg : jpeg_set) {
        if (jpeg.size() >
            std::numeric_limits<uint64_t>::max() - frame_set_bytes) {
          markWriteFailedNoThrow("camera dataset frame is too large");
          return;
        }
        frame_set_bytes += jpeg.size();
      }
      std::lock_guard<std::mutex> lock(mutex_);
      if (!active_.load(std::memory_order_relaxed) || !session_open_ ||
          write_failed_) {
        return;
      }
      const bool exposure_valid =
          metadata.valid && metadata.host_frame_id == frame_id &&
          std::all_of(
              metadata.exposure_us.begin(), metadata.exposure_us.end(),
              [](uint32_t exposure_us) {
                return exposure_us >= prism::kCameraMinExposureUs &&
                       exposure_us <= prism::kCameraMaxExposureUs;
              });
      if (!exposure_valid) {
        ++dropped_frame_sets_;
        return;
      }
      const uint64_t trigger_timestamp_us =
          metadata.trigger_time_ns / 1000ULL;
      if (metadata.trigger_time_ns == 0 ||
          !isPlausibleRkClockRealtimeUs(trigger_timestamp_us) ||
          timestamp_us != trigger_timestamp_us) {
        ++dropped_frame_sets_;
        ++unsynced_camera_frame_sets_dropped_;
        return;
      }
      /*
       * Bound both job count and payload bytes. A slow/removable destination
       * must drop frame sets instead of exhausting Viewer memory.
       */
      constexpr size_t kMaximumQueuedFrameSets = 256;
      if (frame_jobs_.size() >= kMaximumQueuedFrameSets ||
          frame_set_bytes > kMaximumQueuedFrameBytes ||
          queued_payload_bytes_ >
              kMaximumQueuedFrameBytes - frame_set_bytes) {
        ++dropped_frame_sets_;
        return;
      }
      FrameSetJob job;
      job.frame_id = frame_id;
      job.timestamp_us = timestamp_us;
      job.exposure_us = metadata.exposure_us;
      job.payload_bytes = frame_set_bytes;
      job.jpeg = jpeg_set;
      frame_jobs_.push_back(std::move(job));
      queued_payload_bytes_ += frame_set_bytes;
      writer_wakeup_.notify_one();
    } catch (...) {
      markWriteFailedNoThrow("camera dataset queue allocation failed");
    }
  }

  void appendLidar(const prism::LidarPointBatch& batch) {
    try {
      if (!active_.load(std::memory_order_acquire) ||
          mode_.load(std::memory_order_relaxed) !=
              DatasetRecordingMode::Full ||
          !record_lidar_streams_.load(std::memory_order_relaxed) ||
          batch.points.empty()) {
        return;
      }
      if (batch.points.size() >
          std::numeric_limits<uint32_t>::max() / 16u) {
        markWriteFailedNoThrow("LiDAR dataset batch is too large");
        return;
      }
      if (!batch.timestamp_synced ||
          !isPlausibleRkClockRealtimeUs(batch.timestamp_utc_us) ||
          (batch.tai_offset_applied && !batch.timestamp_synced)) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (active_.load(std::memory_order_relaxed) && session_open_ &&
            !write_failed_) {
          ++dropped_lidar_batches_;
          dropped_lidar_points_ += batch.points.size();
          ++unsynced_lidar_batches_dropped_;
          unsynced_lidar_points_dropped_ += batch.points.size();
        }
        return;
      }
      LidarJob job;
      job.timestamp_us = batch.timestamp_utc_us;
      job.timestamp_raw = batch.timestamp_raw;
      job.time_interval_100ns = batch.time_interval_100ns;
      job.timestamp_synced = batch.timestamp_synced;
      job.tai_offset_applied = batch.tai_offset_applied;
      job.batch_id = batch.batch_id;
      job.model = static_cast<uint8_t>(batch.model);
      job.device_type = batch.device_type;
      job.time_type = batch.time_type;
      job.point_count = static_cast<uint32_t>(batch.points.size());
      job.points.reserve(batch.points.size() * 16u);
      auto append_le32 = [&job](uint32_t value) {
        for (unsigned byte = 0; byte < 4u; ++byte) {
          job.points.push_back(
              static_cast<uint8_t>((value >> (byte * 8u)) & 0xffu));
        }
      };
      for (const auto& point : batch.points) {
        append_le32(static_cast<uint32_t>(point.x_mm));
        append_le32(static_cast<uint32_t>(point.y_mm));
        append_le32(static_cast<uint32_t>(point.z_mm));
        job.points.push_back(point.reflectivity);
        job.points.push_back(point.tag);
        job.points.push_back(0u);
        job.points.push_back(0u);
      }
      job.payload_bytes = job.points.size();

      std::lock_guard<std::mutex> lock(mutex_);
      if (!active_.load(std::memory_order_relaxed) || !session_open_ ||
          write_failed_) {
        return;
      }
      constexpr size_t kMaximumQueuedLidarBatches = 512u;
      if (lidar_jobs_.size() >= kMaximumQueuedLidarBatches ||
          job.payload_bytes > kMaximumQueuedFrameBytes ||
          queued_payload_bytes_ >
              kMaximumQueuedFrameBytes - job.payload_bytes) {
        ++dropped_lidar_batches_;
        dropped_lidar_points_ += job.point_count;
        return;
      }
      queued_payload_bytes_ += job.payload_bytes;
      lidar_jobs_.push_back(std::move(job));
      writer_wakeup_.notify_one();
    } catch (...) {
      markWriteFailedNoThrow("LiDAR dataset queue allocation failed");
    }
  }

  void appendLidarImu(const prism::LidarImuSample& sample) {
    try {
      if (!active_.load(std::memory_order_acquire) ||
          !record_lidar_streams_.load(std::memory_order_relaxed)) {
        return;
      }
      std::lock_guard<std::mutex> lock(mutex_);
      if (!active_.load(std::memory_order_relaxed) || !session_open_ ||
          write_failed_ || !lidar_imu_file_.is_open()) {
        return;
      }
      if (!sample.timestamp_synced ||
          !isPlausibleRkClockRealtimeUs(sample.timestamp_utc_us) ||
          (sample.tai_offset_applied && !sample.timestamp_synced)) {
        ++unsynced_lidar_imu_samples_dropped_;
        return;
      }
      writeTumTimestamp(lidar_imu_file_, sample.timestamp_utc_us);
      lidar_imu_file_ << std::fixed << std::setprecision(9);
      for (double value : sample.accel_m_s2) lidar_imu_file_ << ' ' << value;
      for (double value : sample.gyro_rad_s) lidar_imu_file_ << ' ' << value;
      lidar_imu_file_ << ' ' << static_cast<unsigned>(sample.model) << ' '
                      << static_cast<unsigned>(sample.device_type) << ' '
                      << static_cast<unsigned>(sample.time_type) << ' '
                      << sample.sample_id << ' ' << sample.timestamp_raw_ns
                      << ' '
                      << (sample.timestamp_synced ? 1 : 0) << ' '
                      << (sample.tai_offset_applied ? 1 : 0) << '\n';
      if (!lidar_imu_file_.good()) {
        write_failed_ = true;
        write_error_ = "LiDAR IMU dataset write failed";
        return;
      }
      ++lidar_imu_sample_count_;
    } catch (...) {
      markWriteFailedNoThrow("LiDAR IMU dataset write failed");
    }
  }

  DatasetRecordingSummary stop() {
    active_.store(false, std::memory_order_release);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stop_writer_ = true;
    }
    writer_wakeup_.notify_all();
    if (writer_.joinable()) writer_.join();

    std::lock_guard<std::mutex> lock(mutex_);
    DatasetRecordingSummary summary;
    summary.mode = mode_.load(std::memory_order_relaxed);
    summary.had_session = session_open_;
    summary.sample_count = imu_counts_;
    summary.image_count = image_counts_;
    summary.dropped_frame_sets = dropped_frame_sets_;
    summary.lidar_batch_count = lidar_batch_count_;
    summary.lidar_point_count = lidar_point_count_;
    summary.dropped_lidar_batches = dropped_lidar_batches_;
    summary.dropped_lidar_points = dropped_lidar_points_;
    summary.lidar_imu_sample_count = lidar_imu_sample_count_;
    summary.unsynced_imu_samples_dropped =
        unsynced_imu_samples_dropped_;
    summary.unsynced_camera_frame_sets_dropped =
        unsynced_camera_frame_sets_dropped_;
    summary.unsynced_lidar_batches_dropped =
        unsynced_lidar_batches_dropped_;
    summary.unsynced_lidar_points_dropped =
        unsynced_lidar_points_dropped_;
    summary.unsynced_lidar_imu_samples_dropped =
        unsynced_lidar_imu_samples_dropped_;
    if (!session_open_) return summary;

    validateRequiredStreams();
    closeFiles();
    writeManifest(!write_failed_);
    summary.success = !write_failed_;
    summary.error = write_error_;
    if (!summary.success && summary.error.empty()) {
      summary.error = "failed to flush dataset recording";
    }
    session_open_ = false;
    return summary;
  }

  bool isActive() const {
    return active_.load(std::memory_order_acquire);
  }

 private:
  struct FrameSetJob {
    uint32_t frame_id = 0;
    uint64_t timestamp_us = 0;
    uint64_t payload_bytes = 0;
    std::array<uint32_t, 4> exposure_us{};
    std::array<std::vector<uint8_t>, 4> jpeg;
  };

  struct LidarJob {
    uint64_t timestamp_us = 0;
    uint64_t timestamp_raw = 0;
    uint64_t payload_bytes = 0;
    uint32_t batch_id = 0;
    uint32_t point_count = 0;
    uint8_t model = 0;
    uint8_t device_type = 0;
    uint8_t time_type = 0;
    uint16_t time_interval_100ns = 0;
    bool timestamp_synced = false;
    bool tai_offset_applied = false;
    std::vector<uint8_t> points;
  };

  static bool isDatasetDataContainer(const std::filesystem::path& path) {
    if (path.extension() != std::filesystem::path(".bin")) return false;
    const auto stem = path.stem().native();
    const auto camera_prefix =
        std::filesystem::path("camera-data-").native();
    const auto lidar_prefix = std::filesystem::path("lidar-data-").native();
    const auto starts_with = [&stem](const auto& prefix) {
      return stem.size() >= prefix.size() &&
             std::equal(prefix.begin(), prefix.end(), stem.begin());
    };
    return starts_with(camera_prefix) || starts_with(lidar_prefix);
  }

  static uint64_t wallClockUs() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
  }

  static bool isPlausibleRkClockRealtimeUs(uint64_t timestamp_us) {
    // Reject local/monotonic counters while allowing archived RK
    // CLOCK_REALTIME values from well before the product release.
    constexpr uint64_t kMinimumRkClockRealtimeUs = 100000000000000ULL;
    return timestamp_us >= kMinimumRkClockRealtimeUs;
  }

  static void writeTumTimestamp(std::ostream& stream, uint64_t timestamp_us) {
    stream << timestamp_us / 1000000ULL << '.' << std::setw(6)
           << std::setfill('0') << timestamp_us % 1000000ULL
           << std::setfill(' ');
  }

  void writerLoop() noexcept {
    try {
      writerLoopImpl();
    } catch (...) {
      markWriteFailedNoThrow("dataset writer failed");
    }
  }

  void writerLoopImpl() {
    for (;;) {
      std::optional<FrameSetJob> frame_job;
      std::optional<LidarJob> lidar_job;
      {
        std::unique_lock<std::mutex> lock(mutex_);
        writer_wakeup_.wait(lock, [this]() {
          return stop_writer_ || !frame_jobs_.empty() ||
                 !lidar_jobs_.empty();
        });
        if (frame_jobs_.empty() && lidar_jobs_.empty()) {
          if (stop_writer_) return;
          continue;
        }
        const bool take_frame = shouldTakeFrameJob(
            !frame_jobs_.empty(), !lidar_jobs_.empty(),
            frame_jobs_.empty() ? 0 : frame_jobs_.front().timestamp_us,
            lidar_jobs_.empty() ? 0 : lidar_jobs_.front().timestamp_us);
        if (take_frame) {
          frame_job.emplace(std::move(frame_jobs_.front()));
          frame_jobs_.pop_front();
          queued_payload_bytes_ =
              queued_payload_bytes_ >= frame_job->payload_bytes
                  ? queued_payload_bytes_ - frame_job->payload_bytes
                  : 0;
        } else {
          lidar_job.emplace(std::move(lidar_jobs_.front()));
          lidar_jobs_.pop_front();
          queued_payload_bytes_ =
              queued_payload_bytes_ >= lidar_job->payload_bytes
                  ? queued_payload_bytes_ - lidar_job->payload_bytes
                  : 0;
        }
      }
      if (frame_job.has_value()) {
        writeFrameJob(*frame_job);
      } else if (lidar_job.has_value()) {
        writeLidarJob(*lidar_job);
      }
    }
  }

  void writeFrameJob(const FrameSetJob& job) {
    uint64_t frame_set_bytes = 0;
    for (const auto& jpeg : job.jpeg) frame_set_bytes += jpeg.size();
    if (!camera_chunk_file_.is_open() ||
        (camera_chunk_size_ != 0 &&
         camera_chunk_size_ + frame_set_bytes > kCameraChunkTargetBytes)) {
      if (!openNextCameraChunk()) {
        markWriteFailedNoThrow("cannot open camera data container");
        return;
      }
    }

    std::array<uint64_t, 4> offsets{};
    bool payload_ok = true;
    for (size_t camera = 0; camera < job.jpeg.size(); ++camera) {
      offsets[camera] = camera_chunk_size_;
      camera_chunk_file_.write(
          reinterpret_cast<const char*>(job.jpeg[camera].data()),
          static_cast<std::streamsize>(job.jpeg[camera].size()));
      if (!camera_chunk_file_.good()) {
        payload_ok = false;
        break;
      }
      camera_chunk_size_ += job.jpeg[camera].size();
    }
    if (!payload_ok) {
      markWriteFailedNoThrow("camera data container write failed");
      return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (write_failed_) return;
    for (size_t camera = 0; camera < job.jpeg.size(); ++camera) {
      writeTumTimestamp(camera_index_files_[camera], job.timestamp_us);
      camera_index_files_[camera]
          << ' ' << camera_chunk_name_ << ' ' << offsets[camera] << ' '
          << job.jpeg[camera].size() << ' ' << job.exposure_us[camera]
          << '\n';
      if (!camera_index_files_[camera].good()) {
        write_failed_ = true;
        write_error_ = "camera index write failed";
        break;
      }
      ++image_counts_[camera];
    }
  }

  void writeLidarJob(const LidarJob& job) {
    if (!lidar_chunk_file_.is_open() ||
        (lidar_chunk_size_ != 0 &&
         lidar_chunk_size_ + job.points.size() >
             kCameraChunkTargetBytes)) {
      if (!openNextLidarChunk()) {
        markWriteFailedNoThrow("cannot open LiDAR data container");
        return;
      }
    }
    const uint64_t offset = lidar_chunk_size_;
    lidar_chunk_file_.write(
        reinterpret_cast<const char*>(job.points.data()),
        static_cast<std::streamsize>(job.points.size()));
    if (!lidar_chunk_file_.good()) {
      markWriteFailedNoThrow("LiDAR data container write failed");
      return;
    }
    lidar_chunk_size_ += job.points.size();

    std::lock_guard<std::mutex> lock(mutex_);
    if (write_failed_) return;
    writeTumTimestamp(lidar_index_file_, job.timestamp_us);
    lidar_index_file_ << ' ' << lidar_chunk_name_ << ' ' << offset << ' '
                      << job.points.size() << ' ' << job.point_count << ' '
                      << static_cast<unsigned>(job.model) << ' '
                      << static_cast<unsigned>(job.device_type) << ' '
                      << static_cast<unsigned>(job.time_type) << ' '
                      << job.batch_id << ' ' << job.timestamp_raw << ' '
                      << job.time_interval_100ns << ' '
                      << (job.timestamp_synced ? 1 : 0) << ' '
                      << (job.tai_offset_applied ? 1 : 0) << '\n';
    if (!lidar_index_file_.good()) {
      write_failed_ = true;
      write_error_ = "LiDAR index write failed";
      return;
    }
    ++lidar_batch_count_;
    lidar_point_count_ += job.point_count;
  }

  bool openNextCameraChunk() {
    if (camera_chunk_file_.is_open()) {
      camera_chunk_file_.flush();
      camera_chunk_file_.close();
    }
    std::ostringstream name;
    name << "camera-data-" << std::setw(4) << std::setfill('0')
         << camera_chunk_index_++ << ".bin";
    camera_chunk_name_ = name.str();
    camera_chunk_file_.open(root_ / camera_chunk_name_,
                            std::ios::out | std::ios::binary |
                                std::ios::trunc);
    camera_chunk_size_ = 0;
    return camera_chunk_file_.is_open();
  }

  bool openNextLidarChunk() {
    if (lidar_chunk_file_.is_open()) {
      lidar_chunk_file_.flush();
      lidar_chunk_file_.close();
    }
    std::ostringstream name;
    name << "lidar-data-" << std::setw(4) << std::setfill('0')
         << lidar_chunk_index_++ << ".bin";
    lidar_chunk_name_ = name.str();
    lidar_chunk_file_.open(root_ / lidar_chunk_name_,
                           std::ios::out | std::ios::binary |
                               std::ios::trunc);
    lidar_chunk_size_ = 0;
    return lidar_chunk_file_.is_open();
  }

  void markWriteFailedNoThrow(const char* error) noexcept {
    try {
      std::lock_guard<std::mutex> lock(mutex_);
      write_failed_ = true;
      if (write_error_.empty()) write_error_ = error;
    } catch (...) {
      // The recorder must never terminate the Viewer from a worker callback.
    }
  }

  void validateRequiredStreams() {
    std::vector<std::string> missing;
    for (size_t sensor = 0; sensor < imu_counts_.size(); ++sensor) {
      if (imu_counts_[sensor] == 0) {
        missing.push_back("synchronized onboard IMU" +
                          std::to_string(sensor));
      }
    }
    const bool imu_only = mode_.load(std::memory_order_relaxed) ==
                          DatasetRecordingMode::ImuOnly;
    if (!imu_only &&
        std::any_of(image_counts_.begin(), image_counts_.end(),
                    [](uint64_t count) { return count == 0; })) {
      missing.push_back("synchronized four-camera frame sets");
    }
    const bool has_lidar_streams =
        record_lidar_streams_.load(std::memory_order_relaxed);
    if (has_lidar_streams && !imu_only && lidar_batch_count_ == 0) {
      missing.push_back("synchronized LiDAR point batches");
    }
    if (has_lidar_streams && lidar_imu_sample_count_ == 0) {
      missing.push_back("synchronized LiDAR IMU samples");
    }
    if (missing.empty()) return;

    write_failed_ = true;
    if (write_error_.empty()) {
      std::ostringstream detail;
      detail << "aligned dataset contains no ";
      for (size_t index = 0; index < missing.size(); ++index) {
        if (index != 0) detail << (index + 1 == missing.size() ? " or " : ", ");
        detail << missing[index];
      }
      detail << "; unsynchronized samples are never recorded";
      write_error_ = detail.str();
    }
  }

  void writeManifest(bool complete) {
    std::ostringstream manifest;
    manifest.imbue(std::locale::classic());
    const bool imu_only = mode_.load(std::memory_order_relaxed) ==
                          DatasetRecordingMode::ImuOnly;
    const bool has_lidar_streams =
        record_lidar_streams_.load(std::memory_order_relaxed);
    const uint64_t recording_host_end_unix_us = wallClockUs();
    manifest << "format=prism-dataset-v6\n"
             << "complete=" << (complete ? 1 : 0) << "\n"
             << "recording_mode="
             << (imu_only ? "imu-only" : "full")
             << "\n"
             << "image_storage="
             << (imu_only ? "none" : "chunk-v1")
             << "\n"
             << "camera_index="
             << (imu_only ? "none" : "chunk-v2-with-actual-exposure")
             << "\n"
             << "time_domain=rk-clock-realtime\n"
             << "timestamp_epoch=unix\n"
             << "timestamp_policy=strict-synchronized-sensor-time\n"
             << "alignment=common-device-time-domain\n"
             << "timestamp_resolution_us=1\n"
             << "camera_timestamp_reference=trig0-rising-edge\n"
             << "lidar_timestamp_reference=batch-base\n"
             << "point_deskew=none\n"
             << "lidar_storage="
             << (!imu_only && has_lidar_streams
                     ? "cartesian-mm-chunk-v2-with-time-source"
                     : "none")
             << "\n"
             << "lidar_imu_storage="
             << (has_lidar_streams ? "tum-si-v2-with-time-source" : "none")
             << "\n"
             << "chunk_target_bytes=" << kCameraChunkTargetBytes << "\n"
             << "start_unix_us=" << start_unix_us_ << "\n"
             << "end_unix_us=" << recording_host_end_unix_us << "\n"
             << "recording_host_start_unix_us=" << start_unix_us_ << "\n"
             << "recording_host_end_unix_us="
             << recording_host_end_unix_us << "\n"
             << "imu0_samples=" << imu_counts_[0] << "\n"
             << "imu1_samples=" << imu_counts_[1] << "\n"
             << "unsynced_imu0_samples_dropped="
             << unsynced_imu_samples_dropped_[0] << "\n"
             << "unsynced_imu1_samples_dropped="
             << unsynced_imu_samples_dropped_[1] << "\n"
             << "dropped_frame_sets=" << dropped_frame_sets_ << "\n"
             << "unsynced_camera_frame_sets_dropped="
             << unsynced_camera_frame_sets_dropped_ << "\n"
             << "lidar_batches=" << lidar_batch_count_ << "\n"
             << "lidar_points=" << lidar_point_count_ << "\n"
             << "dropped_lidar_batches=" << dropped_lidar_batches_ << "\n"
             << "dropped_lidar_points=" << dropped_lidar_points_ << "\n"
             << "unsynced_lidar_batches_dropped="
             << unsynced_lidar_batches_dropped_ << "\n"
             << "unsynced_lidar_points_dropped="
             << unsynced_lidar_points_dropped_ << "\n"
             << "lidar_imu_samples=" << lidar_imu_sample_count_ << "\n"
             << "unsynced_lidar_imu_samples_dropped="
             << unsynced_lidar_imu_samples_dropped_ << "\n";
    for (size_t camera = 0; camera < image_counts_.size(); ++camera) {
      manifest << "camera" << camera << "_images=" << image_counts_[camera]
               << "\n";
    }
    if (!manifest.good()) {
      write_failed_ = true;
      if (write_error_.empty()) write_error_ = "dataset manifest write failed";
      return;
    }
    const std::string contents = manifest.str();
    QSaveFile output(fromFilesystemPath(root_ / "dataset.info"));
    output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly)) {
      write_failed_ = true;
      if (write_error_.empty()) write_error_ = "cannot write dataset manifest";
      return;
    }
    const qint64 expected_size = static_cast<qint64>(contents.size());
    if (output.write(contents.data(), expected_size) != expected_size ||
        !output.commit()) {
      write_failed_ = true;
      if (write_error_.empty()) {
        write_error_ = "cannot commit dataset manifest";
      }
    }
  }

  void closeFiles() {
    for (auto& file : imu_files_) {
      if (!file.is_open()) continue;
      file.flush();
      if (!file.good()) write_failed_ = true;
      file.close();
    }
    for (auto& file : camera_index_files_) {
      if (!file.is_open()) continue;
      file.flush();
      if (!file.good()) write_failed_ = true;
      file.close();
    }
    if (camera_chunk_file_.is_open()) {
      camera_chunk_file_.flush();
      if (!camera_chunk_file_.good()) write_failed_ = true;
      camera_chunk_file_.close();
    }
    if (lidar_index_file_.is_open()) {
      lidar_index_file_.flush();
      if (!lidar_index_file_.good()) write_failed_ = true;
      lidar_index_file_.close();
    }
    if (lidar_chunk_file_.is_open()) {
      lidar_chunk_file_.flush();
      if (!lidar_chunk_file_.good()) write_failed_ = true;
      lidar_chunk_file_.close();
    }
    if (lidar_imu_file_.is_open()) {
      lidar_imu_file_.flush();
      if (!lidar_imu_file_.good()) write_failed_ = true;
      lidar_imu_file_.close();
    }
  }

  static constexpr uint64_t kCameraChunkTargetBytes =
      8ULL * 1024ULL * 1024ULL * 1024ULL;
  static constexpr uint64_t kMaximumQueuedFrameBytes =
      128ULL * 1024ULL * 1024ULL;
  mutable std::mutex mutex_;
  std::condition_variable writer_wakeup_;
  std::thread writer_;
  std::deque<FrameSetJob> frame_jobs_;
  std::deque<LidarJob> lidar_jobs_;
  std::array<std::ofstream, 2> imu_files_;
  std::array<std::ofstream, 4> camera_index_files_;
  std::ofstream camera_chunk_file_;
  std::ofstream lidar_index_file_;
  std::ofstream lidar_chunk_file_;
  std::ofstream lidar_imu_file_;
  std::filesystem::path root_;
  std::string camera_chunk_name_;
  uint64_t camera_chunk_size_ = 0;
  uint32_t camera_chunk_index_ = 0;
  std::string lidar_chunk_name_;
  uint64_t lidar_chunk_size_ = 0;
  uint32_t lidar_chunk_index_ = 0;
  std::array<uint64_t, 2> imu_counts_{};
  std::array<uint64_t, 4> image_counts_{};
  uint64_t lidar_batch_count_ = 0;
  uint64_t lidar_point_count_ = 0;
  uint64_t dropped_lidar_batches_ = 0;
  uint64_t dropped_lidar_points_ = 0;
  uint64_t lidar_imu_sample_count_ = 0;
  std::array<uint64_t, 2> unsynced_imu_samples_dropped_{};
  uint64_t unsynced_camera_frame_sets_dropped_ = 0;
  uint64_t unsynced_lidar_batches_dropped_ = 0;
  uint64_t unsynced_lidar_points_dropped_ = 0;
  uint64_t unsynced_lidar_imu_samples_dropped_ = 0;
  uint64_t start_unix_us_ = 0;
  uint64_t dropped_frame_sets_ = 0;
  uint64_t queued_payload_bytes_ = 0;
  std::atomic<bool> active_{false};
  std::atomic<DatasetRecordingMode> mode_{DatasetRecordingMode::Full};
  std::atomic<bool> record_lidar_streams_{false};
  bool session_open_ = false;
  bool stop_writer_ = false;
  bool write_failed_ = false;
  std::string write_error_;
};

class ImuPlotWidget : public QWidget {
 public:
  explicit ImuPlotWidget(QWidget* parent = nullptr) : QWidget(parent) {
    setMinimumHeight(280);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    createPanel(&accel_panel_, uiText("Acceleration XYZ (g)", "加速度 XYZ (g)"),
                uiText("Acceleration (g)", "加速度 (g)"), 1.2);
    createPanel(&gyro_panel_, uiText("Gyroscope XYZ (", "角速度 XYZ (") +
                                  QChar(0x00b0) + QStringLiteral("/s)"),
                uiText("Angular rate (", "角速度 (") + QChar(0x00b0) +
                    QStringLiteral("/s)"),
                10.0);
    updateUnitAppearance();
    layout->addWidget(accel_panel_.view, 1);
    layout->addWidget(gyro_panel_.view, 1);

    refresh_timer_ = new QTimer(this);
    refresh_timer_->setInterval(
        static_cast<int>(kImuPlotRefreshPeriod.count()));
    refresh_timer_->setTimerType(Qt::PreciseTimer);
    connect(refresh_timer_, &QTimer::timeout, this, [this]() {
      if (!active_ || !dirty_) return;
      refreshCharts();
    });
  }

  void setSensor(int sensor) {
    if (sensor < 0 || sensor >= static_cast<int>(series_.size())) return;
    selected_sensor_ = sensor;
    dirty_ = true;
    if (active_) refreshCharts();
  }

  void setUnits(AccelerationUnit acceleration_unit,
                AngularVelocityUnit angular_velocity_unit) {
    if (acceleration_unit_ == acceleration_unit &&
        angular_velocity_unit_ == angular_velocity_unit) {
      return;
    }
    acceleration_unit_ = acceleration_unit;
    angular_velocity_unit_ = angular_velocity_unit;
    updateUnitAppearance();
    dirty_ = true;
    if (active_) refreshCharts();
  }

  void setActive(bool active) {
    if (active_ == active) return;
    active_ = active;
    if (active_) {
      refresh_timer_->start();
      refreshCharts();
    } else {
      refresh_timer_->stop();
    }
  }

  void clear() {
    for (auto& samples : series_) samples.clear();
    dirty_ = true;
    if (active_) refreshCharts();
  }

  void appendSample(
      const prism::ImuSample& sample,
      std::chrono::steady_clock::time_point received_at) {
    if (sample.sensor_id >= series_.size()) return;
    auto& samples = series_[sample.sensor_id];
    PlotSample point;
    point.time_s = std::chrono::duration<double>(
        received_at.time_since_epoch()).count();
    for (size_t axis = 0; axis < 3; ++axis) {
      point.accel_mg[axis] = static_cast<double>(sample.accel_mg[axis]);
      point.gyro_mdps[axis] = static_cast<double>(sample.gyro_mdps[axis]);
    }
    samples.push_back(point);
    while (!samples.empty() &&
           (samples.size() > 1200 || point.time_s - samples.front().time_s > 15.0)) {
      samples.pop_front();
    }
    if (static_cast<int>(sample.sensor_id) == selected_sensor_) dirty_ = true;
  }

 private:
  struct PlotSample {
    double time_s = 0.0;
    std::array<double, 3> accel_mg{};
    std::array<double, 3> gyro_mdps{};
  };

  struct ChartPanel {
    QChartView* view = nullptr;
    QChart* chart = nullptr;
    QValueAxis* x_axis = nullptr;
    QValueAxis* y_axis = nullptr;
    std::array<QLineSeries*, 3> series{};
    QLineSeries* zero_line = nullptr;
    double initial_scale = 1.0;
    double minimum_span = 0.01;
  };

  void createPanel(ChartPanel* panel, const QString& title,
                   const QString& y_title, double initial_scale) {
    panel->chart = new QChart();
    panel->initial_scale = initial_scale;
    panel->chart->setTitle(title);
    panel->chart->setAnimationOptions(QChart::NoAnimation);
    panel->chart->setDropShadowEnabled(false);
    panel->chart->setBackgroundRoundness(6.0);
    panel->chart->setMargins(QMargins(4, 2, 4, 2));

    panel->x_axis = new QValueAxis(panel->chart);
    panel->x_axis->setRange(-10.0, 0.0);
    panel->x_axis->setTickCount(6);
    panel->x_axis->setLabelFormat("%.1f s");
    panel->x_axis->setTitleText(uiText("Time to now", "距当前时间"));
    panel->x_axis->setGridLineColor(QColor(QStringLiteral("#e8edf4")));
    panel->y_axis = new QValueAxis(panel->chart);
    panel->y_axis->setRange(-initial_scale, initial_scale);
    panel->y_axis->setTickCount(5);
    panel->y_axis->setLabelFormat("%.2f");
    panel->y_axis->setTitleText(y_title);
    panel->y_axis->setGridLineColor(QColor(QStringLiteral("#e8edf4")));
    panel->chart->addAxis(panel->x_axis, Qt::AlignBottom);
    panel->chart->addAxis(panel->y_axis, Qt::AlignLeft);

    const std::array<QColor, 3> colors = {
        QColor(QStringLiteral("#d92d20")), QColor(QStringLiteral("#12b76a")),
        QColor(QStringLiteral("#1570ef"))};
    const std::array<QString, 3> names = {
        QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z")};
    for (size_t axis = 0; axis < panel->series.size(); ++axis) {
      panel->series[axis] = new QLineSeries(panel->chart);
      panel->series[axis]->setName(names[axis]);
      QPen pen(colors[axis], 1.5);
      pen.setCosmetic(true);
      panel->series[axis]->setPen(pen);
      panel->chart->addSeries(panel->series[axis]);
      panel->series[axis]->attachAxis(panel->x_axis);
      panel->series[axis]->attachAxis(panel->y_axis);
    }

    panel->zero_line = new QLineSeries(panel->chart);
    QPen zero_pen(QColor(QStringLiteral("#667085")), 1.0, Qt::DashLine);
    zero_pen.setCosmetic(true);
    panel->zero_line->setPen(zero_pen);
    panel->zero_line->setName(uiText("Zero", "零线"));
    panel->zero_line->append(-10.0, 0.0);
    panel->zero_line->append(0.0, 0.0);
    panel->chart->addSeries(panel->zero_line);
    panel->zero_line->attachAxis(panel->x_axis);
    panel->zero_line->attachAxis(panel->y_axis);
    panel->chart->legend()->markers(panel->zero_line).front()->setVisible(false);
    panel->chart->legend()->setAlignment(Qt::AlignTop);

    panel->view = new QChartView(panel->chart, this);
    // Dynamic QtCharts antialiasing is expensive and provides little benefit
    // for one-pixel live traces. Static labels and axes remain unchanged.
    panel->view->setRenderHint(QPainter::Antialiasing, false);
    panel->view->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    panel->view->setMinimumHeight(132);
  }

  void updateUnitAppearance() {
    const QString acceleration_unit = accelerationUnitText(acceleration_unit_);
    accel_panel_.chart->setTitle(
        uiText("Acceleration XYZ (%1)", "加速度 XYZ (%1)")
            .arg(acceleration_unit));
    accel_panel_.y_axis->setTitleText(
        uiText("Acceleration (%1)", "加速度 (%1)")
            .arg(acceleration_unit));
    accel_panel_.initial_scale = std::abs(convertAcceleration(
        1200.0, AccelerationUnit::MilliGravity, acceleration_unit_));
    accel_panel_.minimum_span = std::abs(convertAcceleration(
        2.0, AccelerationUnit::MilliGravity, acceleration_unit_));
    switch (acceleration_unit_) {
      case AccelerationUnit::MilliGravity:
        accel_panel_.y_axis->setLabelFormat("%.1f");
        break;
      case AccelerationUnit::Gravity:
        accel_panel_.y_axis->setLabelFormat("%.4f");
        break;
      case AccelerationUnit::MetresPerSecondSquared:
        accel_panel_.y_axis->setLabelFormat("%.3f");
        break;
    }

    const QString angular_velocity_unit =
        angularVelocityUnitText(angular_velocity_unit_);
    gyro_panel_.chart->setTitle(
        uiText("Gyroscope XYZ (%1)", "角速度 XYZ (%1)")
            .arg(angular_velocity_unit));
    gyro_panel_.y_axis->setTitleText(
        uiText("Angular rate (%1)", "角速度 (%1)")
            .arg(angular_velocity_unit));
    gyro_panel_.initial_scale = std::abs(convertAngularVelocity(
        10000.0, AngularVelocityUnit::MilliDegreesPerSecond,
        angular_velocity_unit_));
    gyro_panel_.minimum_span = std::abs(convertAngularVelocity(
        20.0, AngularVelocityUnit::MilliDegreesPerSecond,
        angular_velocity_unit_));
    if (angular_velocity_unit_ ==
        AngularVelocityUnit::MilliDegreesPerSecond) {
      gyro_panel_.y_axis->setLabelFormat("%.0f");
    } else if (angular_velocity_unit_ ==
               AngularVelocityUnit::RadiansPerSecond) {
      gyro_panel_.y_axis->setLabelFormat("%.5f");
    } else {
      gyro_panel_.y_axis->setLabelFormat("%.3f");
    }
  }

  void refreshPanel(ChartPanel* panel, bool acceleration) {
    const auto& samples = series_[selected_sensor_];
    const double end_time = samples.empty() ? 0.0 : samples.back().time_s;
    bool have_visible_value = false;
    double visible_min = 0.0;
    double visible_max = 0.0;
    for (size_t axis = 0; axis < panel->series.size(); ++axis) {
      QList<QPointF> points;
      points.reserve(static_cast<qsizetype>(samples.size()));
      for (const auto& point : samples) {
        const double x = point.time_s - end_time;
        if (x < -10.0) continue;
        const double value =
            acceleration
                ? convertAcceleration(point.accel_mg[axis],
                                      AccelerationUnit::MilliGravity,
                                      acceleration_unit_)
                : convertAngularVelocity(
                      point.gyro_mdps[axis],
                      AngularVelocityUnit::MilliDegreesPerSecond,
                      angular_velocity_unit_);
        points.append(QPointF(x, value));
        if (!have_visible_value) {
          visible_min = value;
          visible_max = value;
          have_visible_value = true;
        } else {
          visible_min = std::min(visible_min, value);
          visible_max = std::max(visible_max, value);
        }
      }
      panel->series[axis]->replace(points);
    }
    if (!have_visible_value) {
      panel->y_axis->setRange(-panel->initial_scale, panel->initial_scale);
      return;
    }

    // Use the extrema of exactly the samples currently visible on the chart.
    // Only widen a constant-value range enough for QValueAxis to remain valid.
    const double minimum_span = panel->minimum_span;
    if (visible_max - visible_min < minimum_span) {
      const double center = (visible_min + visible_max) * 0.5;
      visible_min = center - minimum_span * 0.5;
      visible_max = center + minimum_span * 0.5;
    }
    panel->y_axis->setRange(visible_min, visible_max);
  }

  void refreshCharts() {
    refreshPanel(&accel_panel_, true);
    refreshPanel(&gyro_panel_, false);
    dirty_ = false;
  }

  std::array<std::deque<PlotSample>, 2> series_;
  ChartPanel accel_panel_;
  ChartPanel gyro_panel_;
  QTimer* refresh_timer_ = nullptr;
  AccelerationUnit acceleration_unit_ =
      prism_viewer::imu_units::kDefaultAccelerationUnit;
  AngularVelocityUnit angular_velocity_unit_ =
      prism_viewer::imu_units::kDefaultAngularVelocityUnit;
  int selected_sensor_ = 0;
  bool active_ = false;
  bool dirty_ = true;
};

WifiHotspotViewState toWifiHotspotViewState(
    const prism::WifiHotspotStatus& status) {
  WifiHotspotViewState view;
  view.present = status.present;
  view.enabled = status.enabled;
  view.running = status.running;
  view.ap_running = status.ap_running;
  view.dhcp_running = status.dhcp_running;
  view.persisted = status.persisted;
  view.error_code = status.error_code;
  view.interface_name = toQString(status.interface_name);
  view.ssid = toQString(status.ssid);
  view.address = toQString(status.address);
  view.error = toQString(status.error);
  return view;
}

class MainWindow : public QMainWindow {
 public:
  MainWindow() {
    setWindowTitle(QStringLiteral("Prism Viewer"));
    setWindowIcon(QIcon(QStringLiteral(":/branding/prism-mark.png")));
    resize(1480, 940);

    auto* central = new QWidget(this);
    central->setObjectName(QStringLiteral("appRoot"));
    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(14, 14, 14, 12);
    root->setSpacing(12);

    central->setStyleSheet(QStringLiteral(
        "QWidget { color: #182230; font-size: 10pt; }"
        "QWidget#appRoot { background: #f5f7fb; }"
        "QFrame#headerCard { background: #ffffff; border: 1px solid #e1e7ef;"
        "                    border-radius: 12px; }"
        "QGroupBox { background: #ffffff; border: 1px solid #dfe6ef; border-radius: 10px;"
        "            margin-top: 14px; padding: 11px; font-weight: 600; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 5px;"
        "                   color: #52637a; }"
        "QPushButton { background: #ffffff; border: 1px solid #c9d4e2; border-radius: 7px;"
        "              padding: 7px 13px; }"
        "QPushButton:hover { background: #edf4ff; border-color: #7ba7e7; }"
        "QPushButton:pressed { background: #dbeafe; }"
        "QComboBox, QAbstractSpinBox, QLineEdit { background: #ffffff;"
        "                       color: #182230; border: 1px solid #c9d4e2;"
        "                       border-radius: 7px; padding: 6px 9px;"
        "                       selection-background-color: #1557d2;"
        "                       selection-color: #ffffff; }"
        "QComboBox:focus, QAbstractSpinBox:focus, QLineEdit:focus {"
        "                       border-color: #4b83d1; }"
        "QTabWidget::pane { background: #f5f7fb; border: 1px solid #dfe6ef;"
        "                   border-radius: 9px; top: -1px; }"
        "QTabBar::tab { background: #e9eef5; color: #52637a; padding: 8px 16px;"
        "               margin-right: 3px; border-top-left-radius: 7px;"
        "               border-top-right-radius: 7px; font-weight: 600; }"
        "QTabBar::tab:selected { background: #ffffff; color: #1557d2; }"
        "QTabBar::tab:hover:!selected { background: #dfe7f2; }"
        "QPushButton#imuSelectButton { min-width: 72px; padding: 6px 14px;"
        "                              background: #ffffff; color: #344054;"
        "                              border: 1px solid #b8c7da; font-weight: 600; }"
        "QPushButton#imuSelectButton:checked { background: #1557d2; color: #ffffff;"
        "                                      border-color: #1557d2; }"
        "QPushButton#imuSelectButton:checked:hover { background: #124bb5; }"
        "QPushButton#startButton { background: #1557d2; color: white; border-color: #1557d2;"
        "                          font-weight: 600; }"
        "QPushButton#stopButton { background: #b42318; color: white; border-color: #b42318;"
        "                         font-weight: 600; }"
        "QPushButton:disabled, QPushButton#startButton:disabled,"
        "QPushButton#stopButton:disabled, QPushButton#imuSelectButton:disabled {"
        "  background: #e4e7ec; color: #98a2b3; border-color: #d0d5dd; }"
        "QComboBox:disabled, QAbstractSpinBox:disabled, QLineEdit:disabled {"
        "  background: #e4e7ec; color: #98a2b3; border-color: #d0d5dd; }"
        "QHeaderView { background: #f5f7fb; }"
        "QHeaderView::section, QTableCornerButton::section {"
        "  background: #e9eef5; color: #24364d; border: 0;"
        "  border-right: 1px solid #c9d4e2;"
        "  border-bottom: 1px solid #c9d4e2; padding: 6px 8px;"
        "  font-weight: 600; }"
        "QPlainTextEdit, QTableWidget { background: #ffffff; border: 1px solid #d9e2ef;"
        "  alternate-background-color: #f7f9fc; gridline-color: #d9e2ef;"
        "  selection-background-color: #dbeafe; selection-color: #182230;"
        "  border-radius: 7px; }"
        "QFrame#cameraTile { background: #ffffff; border: 1px solid #dfe6ef;"
        "                    border-radius: 10px; }"
        "QFrame#cameraTile:hover { border-color: #93b4df; }"
        "QFrame#cameraTile QLabel { background: transparent; }"
        "QLabel#cameraCaption { color: #24364d; font-weight: 700; font-size: 10.5pt; }"
        "QLabel#cameraStats { color: #607089; font-size: 9pt; }"
        "QLabel#cameraImage { background: #080d16; color: #a8b4c5;"
        "                     border: 1px solid #202b3c; border-radius: 7px; }"
        "QSplitter::handle { background: #e4e9f0; width: 8px; }"));

    auto* header_card = new QFrame(central);
    header_card->setObjectName(QStringLiteral("headerCard"));
    auto* header_layout = new QVBoxLayout(header_card);
    header_layout->setContentsMargins(16, 12, 16, 12);
    header_layout->setSpacing(9);
    auto* header = new QHBoxLayout();
    auto* brand_mark = new QLabel(header_card);
    brand_mark->setFixedSize(52, 52);
    brand_mark->setAlignment(Qt::AlignCenter);
    brand_mark->setPixmap(
        QPixmap(QStringLiteral(":/branding/prism-mark.png"))
            .scaled(48, 48, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    brand_mark->setToolTip(QStringLiteral("Prism"));
    header->addWidget(brand_mark, 0, Qt::AlignVCenter);
    auto* title_stack = new QVBoxLayout();
    auto* title = new QLabel(QStringLiteral("Prism Viewer"), header_card);
    title->setStyleSheet(QStringLiteral(
        "font-size: 21pt; font-weight: 750; color: #101828;"));
    auto* subtitle = new QLabel(
        uiText("USB SDK based 4-channel MJPEG preview, metadata, and IMU monitor",
               "基于 USB SDK 的四路相机预览、元数据和 IMU 监视器"),
        header_card);
    subtitle->setStyleSheet(QStringLiteral("color: #667085;"));
    title_stack->addWidget(title);
    title_stack->addWidget(subtitle);
    header->addLayout(title_stack);
    header->addStretch(1);

    auto* device_label = new QLabel(uiText("Device", "设备"), header_card);
    device_label->setStyleSheet(QStringLiteral(
        "color: #52637a; font-weight: 600;"));
    header->addWidget(device_label);
    device_selector_ = new QComboBox(header_card);
    device_selector_->setMinimumWidth(250);
    device_selector_->setToolTip(
        uiText("Select a Prism USB serial number", "选择 Prism USB 序列号"));
    refresh_devices_button_ = new QPushButton(uiText("Refresh", "刷新"), header_card);
    open_device_button_ = new QPushButton(uiText("Open Device", "打开设备"), header_card);
    close_device_button_ = new QPushButton(uiText("Close Device", "关闭设备"), header_card);
    start_button_ = new QPushButton(uiText("Start Capture", "开始采集"), header_card);
    stop_button_ = new QPushButton(uiText("Stop", "停止"), header_card);
    imu_record_start_button_ = new QPushButton(
        uiText("Record...", "录制..."), header_card);
    auto* recording_menu = new QMenu(imu_record_start_button_);
    auto* record_full_dataset_action = recording_menu->addAction(
        uiText("Full Dataset (Cameras + Onboard IMUs + enabled LiDAR)...",
               "完整数据集（相机 + 板载 IMU + 已启用雷达）..."));
    auto* record_imu_only_action = recording_menu->addAction(
        uiText("IMU Only (Onboard + enabled LiDAR IMU)...",
               "仅录制 IMU（板载 + 已启用雷达 IMU）..."));
    imu_record_start_button_->setMenu(recording_menu);
    imu_record_start_button_->setToolTip(
        uiText("Full mode records cameras and onboard IMU0/IMU1, plus LiDAR "
               "points and LiDAR IMU when LiDAR is enabled. IMU-only mode "
               "records onboard IMU0/IMU1 and the enabled LiDAR IMU as "
               "separate streams.",
               "完整模式录制相机和板载 IMU0/IMU1；启用雷达时还会录制雷达"
               "点云和雷达 IMU。仅 IMU 模式分别录制板载 IMU0/IMU1 与已启用"
               "的雷达 IMU。"));
    connect(record_full_dataset_action, &QAction::triggered, this, [this]() {
      startImuRecording(DatasetRecordingMode::Full);
    });
    connect(record_imu_only_action, &QAction::triggered, this, [this]() {
      startImuRecording(DatasetRecordingMode::ImuOnly);
    });
    imu_record_stop_button_ = new QPushButton(
        uiText("Stop Recording", "停止录制"), header_card);
    host_time_sync_button_ = new QPushButton(
        uiText("Set Device Time", "校准设备时间"), header_card);
    system_upgrade_button_ = new QPushButton(
        uiText("Upgrade System", "系统升级"), header_card);
    log_button_ = new QPushButton(uiText("Open Log", "打开日志"), header_card);
    language_selector_ = new QComboBox(header_card);
    language_selector_->addItem(QString::fromUtf8(u8"中文"), QStringLiteral("zh_CN"));
    language_selector_->addItem(QStringLiteral("English"), QStringLiteral("en"));
    language_selector_->setCurrentIndex(
        prism_viewer::common::chineseUi() ? 0 : 1);
    language_selector_->setToolTip(
        uiText("Change display language (the viewer restarts once)",
               "切换显示语言（Viewer 会自动重启一次）"));
    start_button_->setObjectName(QStringLiteral("startButton"));
    stop_button_->setObjectName(QStringLiteral("stopButton"));
    imu_record_start_button_->setMinimumWidth(130);
    imu_record_stop_button_->setMinimumWidth(130);
    close_device_button_->setEnabled(false);
    start_button_->setEnabled(false);
    stop_button_->setEnabled(false);
    imu_record_start_button_->setEnabled(false);
    imu_record_stop_button_->setEnabled(false);
    host_time_sync_button_->setEnabled(false);
    system_upgrade_button_->setEnabled(false);
    header->addWidget(device_selector_);
    header->addWidget(refresh_devices_button_);
    header->addWidget(open_device_button_);
    header->addWidget(close_device_button_);
    header_layout->addLayout(header);

    auto* actions = new QHBoxLayout();
    actions->setSpacing(8);
    actions->addWidget(start_button_);
    actions->addWidget(stop_button_);
    actions->addSpacing(8);
    actions->addWidget(imu_record_start_button_);
    actions->addWidget(imu_record_stop_button_);
    actions->addStretch(1);
    actions->addWidget(host_time_sync_button_);
    actions->addWidget(system_upgrade_button_);
    actions->addWidget(log_button_);
    actions->addWidget(language_selector_);
    header_layout->addLayout(actions);
    root->addWidget(header_card);

    auto* status_strip = new QHBoxLayout();
    status_strip->setSpacing(8);
    status_label_ = new QLabel(uiText("Device closed", "设备已关闭"), central);
    status_label_->setWordWrap(true);
    status_label_->setStyleSheet(QStringLiteral(
        "background: #ffffff; border: 1px solid #d9e2ef; border-radius: 6px;"
        "padding: 8px 10px; color: #344054;"));
    status_strip->addWidget(status_label_, 2);

    time_sync_label_ = new QLabel(
        uiText("Time sync: waiting for DeviceInfo", "时间同步：等待 DeviceInfo"), central);
    time_sync_label_->setWordWrap(true);
    time_sync_label_->setStyleSheet(QStringLiteral(
        "background: #f2f4f7; color: #475467; border: 1px solid #d0d5dd;"
        "border-radius: 6px; padding: 7px 10px; font-weight: 600;"));
    status_strip->addWidget(time_sync_label_, 1);

    host_time_sync_label_ = new QLabel(
        uiText("Host/device clock: not measured",
               "主机/设备时钟：尚未测量"),
        central);
    host_time_sync_label_->setWordWrap(true);
    host_time_sync_label_->setStyleSheet(QStringLiteral(
        "background: #f2f4f7; color: #475467; border: 1px solid #d0d5dd;"
        "border-radius: 6px; padding: 7px 10px; font-weight: 600;"));
    status_strip->addWidget(host_time_sync_label_, 1);
    root->addLayout(status_strip);

    tabs_ = new QTabWidget(central);
    // QTabWidget normally contributes the maximum minimum-size hint of every
    // page to its parent. The LiDAR controls and point-cloud canvas can then
    // make the main window taller than the Linux desktop work area even while
    // another tab is selected. The window manager marks such a window as
    // maximized but cannot shrink it to the work area. Let the root layout
    // treat the tab viewport as shrinkable; each active page still receives
    // all available space and its own layouts remain intact.
    tabs_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);

    device_info_panel_ = new DeviceInfoPanel(tabs_);
    tabs_->addTab(device_info_panel_, uiText("Device Info", "设备信息"));

    camera_page_ = new QWidget(tabs_);
    auto* camera_layout = new QVBoxLayout(camera_page_);
    camera_layout->setContentsMargins(8, 8, 8, 8);
    camera_layout->setSpacing(10);
    tabs_->addTab(camera_page_, uiText("Camera", "相机"));

    auto* camera_splitter = new QSplitter(Qt::Horizontal, camera_page_);
    camera_splitter->setChildrenCollapsible(false);
    camera_splitter->setHandleWidth(8);
    camera_layout->addWidget(camera_splitter, 1);

    imu_page_ = new QWidget(tabs_);
    auto* imu_page_layout = new QVBoxLayout(imu_page_);
    imu_page_layout->setContentsMargins(8, 8, 8, 8);
    imu_page_layout->setSpacing(10);
    tabs_->addTab(imu_page_, QStringLiteral("IMU"));

    lidar_page_ = new QWidget(tabs_);
    auto* lidar_layout = new QVBoxLayout(lidar_page_);
    lidar_layout->setContentsMargins(8, 8, 8, 8);
    lidar_layout->setSpacing(10);
    tabs_->addTab(lidar_page_, QStringLiteral("LiDAR"));

    auto* lidar_controls = new QGroupBox(
        uiText("Livox point cloud", "Livox 点云"), lidar_page_);
    auto* lidar_controls_layout = new QHBoxLayout(lidar_controls);
    lidar_enabled_checkbox_ = new QCheckBox(
        uiText("Include LiDAR in capture", "采集时启用雷达"),
        lidar_controls);
    lidar_model_selector_ = new QComboBox(lidar_controls);
    lidar_model_selector_->addItem(
        uiText("Select model...", "请选择雷达型号……"),
        static_cast<int>(prism::LidarModel::None));
    lidar_model_selector_->addItem(
        QStringLiteral("Mid-360"),
        static_cast<int>(prism::LidarModel::Mid360));
    lidar_model_selector_->addItem(
        QStringLiteral("Mid-360S"),
        static_cast<int>(prism::LidarModel::Mid360S));
    lidar_model_selector_->setCurrentIndex(0);
    lidar_model_selector_->setEnabled(false);
    lidar_model_selector_->setMinimumWidth(180);
    lidar_model_selector_->setToolTip(uiText(
        "Model selection is mandatory; the Agent does not auto-detect it",
        "必须明确选择型号，Agent 不会自动猜测"));
    auto* lidar_point_size_label = new QLabel(
        uiText("Point size", "点大小"), lidar_controls);
    lidar_point_size_spin_ = new QSpinBox(lidar_controls);
    lidar_point_size_spin_->setObjectName(
        QStringLiteral("lidarPointSizeSpin"));
    lidar_point_size_spin_->setRange(
        prism_viewer::LidarPointCloudWidget::kMinimumPointSize,
        prism_viewer::LidarPointCloudWidget::kMaximumPointSize);
    lidar_point_size_spin_->setSuffix(QStringLiteral(" px"));
    lidar_point_size_spin_->setToolTip(uiText(
        "Change the live point-cloud rendering size",
        "调整实时点云的渲染点大小"));
    bool saved_lidar_point_size_valid = false;
    const int saved_lidar_point_size_value =
        QSettings(QStringLiteral("DIBULI"), QStringLiteral("PrismViewer"))
            .value(QStringLiteral("lidar/point_size"),
                   prism_viewer::LidarPointCloudWidget::kDefaultPointSize)
            .toInt(&saved_lidar_point_size_valid);
    const int saved_lidar_point_size = saved_lidar_point_size_valid
        ? saved_lidar_point_size_value
        : prism_viewer::LidarPointCloudWidget::kDefaultPointSize;
    lidar_point_size_spin_->setValue(std::clamp(
        saved_lidar_point_size,
        prism_viewer::LidarPointCloudWidget::kMinimumPointSize,
        prism_viewer::LidarPointCloudWidget::kMaximumPointSize));
    auto* lidar_top_view_button = new QPushButton(
        uiText("Top view", "俯视"), lidar_controls);
    lidar_top_view_button->setObjectName(
        QStringLiteral("lidarTopViewButton"));
    lidar_top_view_button->setMinimumWidth(96);
    lidar_top_view_button->setToolTip(uiText(
        "Look straight down at the LiDAR XY plane",
        "垂直俯视雷达 XY 平面"));
    auto* lidar_reset_view_button = new QPushButton(
        uiText("Reset view", "重置视角"), lidar_controls);
    lidar_reset_view_button->setObjectName(
        QStringLiteral("lidarResetViewButton"));
    lidar_reset_view_button->setMinimumWidth(96);
    lidar_reset_view_button->setToolTip(uiText(
        "Restore the default LiDAR view and zoom",
        "恢复默认雷达视角和缩放"));
    lidar_status_label_ = new QLabel(
        uiText("LiDAR disabled", "雷达未启用"), lidar_controls);
    lidar_status_label_->setStyleSheet(QStringLiteral(
        "background: #f2f4f7; color: #475467; border: 1px solid #d0d5dd;"
        "border-radius: 6px; padding: 7px 10px; font-weight: 600;"));
    lidar_status_label_->setWordWrap(true);
    lidar_controls_layout->addWidget(lidar_enabled_checkbox_);
    lidar_controls_layout->addWidget(lidar_model_selector_);
    lidar_controls_layout->addWidget(lidar_point_size_label);
    lidar_controls_layout->addWidget(lidar_point_size_spin_);
    lidar_controls_layout->addWidget(lidar_top_view_button);
    lidar_controls_layout->addWidget(lidar_reset_view_button);
    lidar_controls_layout->addWidget(lidar_status_label_, 1);
    lidar_layout->addWidget(lidar_controls);

    auto* lidar_network_group = new QGroupBox(
        uiText("end0 / Mid360 network", "end0 / Mid360 网络"), lidar_page_);
    auto* lidar_network_root = new QVBoxLayout(lidar_network_group);
    auto* lidar_network_form = new QFormLayout();
    lidar_network_enabled_checkbox_ = new QCheckBox(
        uiText("Enable end0", "启用 end0"), lidar_network_group);
    lidar_network_host_ip_ = new QLineEdit(
        QStringLiteral("192.168.1.5"), lidar_network_group);
    lidar_network_netmask_ = new QLineEdit(
        QStringLiteral("255.255.255.0"), lidar_network_group);
    lidar_network_target_ip_ = new QLineEdit(
        QStringLiteral("192.168.1.3"), lidar_network_group);
    lidar_network_host_ip_->setMaximumWidth(180);
    lidar_network_netmask_->setMaximumWidth(180);
    lidar_network_target_ip_->setMaximumWidth(180);
    lidar_network_form->addRow(QString(), lidar_network_enabled_checkbox_);
    lidar_network_form->addRow(
        uiText("RK end0 IPv4", "RK end0 IPv4"), lidar_network_host_ip_);
    lidar_network_form->addRow(
        uiText("Subnet mask", "子网掩码"), lidar_network_netmask_);
    lidar_network_form->addRow(
        uiText("Mid360 IPv4", "Mid360 IPv4"), lidar_network_target_ip_);
    lidar_network_root->addLayout(lidar_network_form);
    auto* lidar_network_actions = new QHBoxLayout();
    lidar_network_refresh_button_ = new QPushButton(
        uiText("Refresh", "刷新"), lidar_network_group);
    lidar_network_apply_button_ = new QPushButton(
        uiText("Save and apply", "保存并应用"), lidar_network_group);
    lidar_network_probe_button_ = new QPushButton(
        uiText("Test connection", "测试连接"), lidar_network_group);
    lidar_network_actions->addWidget(lidar_network_refresh_button_);
    lidar_network_actions->addWidget(lidar_network_apply_button_);
    lidar_network_actions->addWidget(lidar_network_probe_button_);
    lidar_network_actions->addStretch(1);
    lidar_network_root->addLayout(lidar_network_actions);
    lidar_network_status_label_ = new QLabel(
        uiText("Open a device to read end0 settings",
               "打开设备后读取 end0 设置"),
        lidar_network_group);
    lidar_network_status_label_->setWordWrap(true);
    lidar_network_status_label_->setStyleSheet(QStringLiteral(
        "background: #f2f4f7; color: #475467; border: 1px solid #d0d5dd;"
        "border-radius: 6px; padding: 7px 10px; font-weight: 600;"));
    lidar_network_root->addWidget(lidar_network_status_label_);
    lidar_layout->addWidget(lidar_network_group);
    lidar_point_cloud_widget_ =
        new prism_viewer::LidarPointCloudWidget(lidar_page_);
    lidar_point_cloud_widget_->setPointSize(
        lidar_point_size_spin_->value());
    connect(lidar_top_view_button, &QPushButton::clicked,
            lidar_point_cloud_widget_,
            &prism_viewer::LidarPointCloudWidget::setTopView);
    connect(lidar_reset_view_button, &QPushButton::clicked,
            lidar_point_cloud_widget_,
            &prism_viewer::LidarPointCloudWidget::resetView);
    lidar_layout->addWidget(lidar_point_cloud_widget_, 1);

    wifi_hotspot_panel_ = new WifiHotspotPanel(tabs_);
    tabs_->addTab(wifi_hotspot_panel_, uiText("Network", "网络"));

    dataset_page_ = new QWidget(tabs_);
    auto* dataset_layout = new QVBoxLayout(dataset_page_);
    dataset_layout->setContentsMargins(8, 8, 8, 8);
    dataset_layout->setSpacing(10);
    tabs_->addTab(dataset_page_,
                  uiText("Local Datasets", "本地数据集"));
    root->addWidget(tabs_, 1);

    auto* video_group = new QGroupBox(
        uiText("Live cameras", "实时相机"), camera_splitter);
    auto* video_grid = new QGridLayout(video_group);
    video_grid->setContentsMargins(10, 14, 10, 10);
    video_grid->setHorizontalSpacing(10);
    video_grid->setVerticalSpacing(10);
    video_grid->setColumnStretch(0, 1);
    video_grid->setColumnStretch(1, 1);
    video_grid->setRowStretch(0, 1);
    video_grid->setRowStretch(1, 1);
    for (int i = 0; i < 4; ++i) {
      auto* tile = new QFrame(video_group);
      tile->setObjectName(QStringLiteral("cameraTile"));
      auto* tile_layout = new QVBoxLayout(tile);
      tile_layout->setContentsMargins(9, 8, 9, 9);
      tile_layout->setSpacing(7);

      auto* caption = new QLabel(uiText("Camera %1", "相机 %1").arg(i), tile);
      caption->setObjectName(QStringLiteral("cameraCaption"));
      image_labels_[i] = new ImageViewLabel(tile);
      image_labels_[i]->setObjectName(QStringLiteral("cameraImage"));
      image_labels_[i]->setTransformationMode(Qt::FastTransformation);
      image_labels_[i]->setMinimumSize(280, 200);
      image_labels_[i]->setCursor(Qt::PointingHandCursor);
      image_labels_[i]->clearImage(uiText("No frame", "无图像"));
      image_labels_[i]->setToolTip(
          uiText("Click to enlarge with four-camera thumbnails",
                 "点击可在新窗口放大查看，并使用四路缩略图切换"));
      image_labels_[i]->on_click = [this, i]() { showLiveCameraZoom(i); };
      if (i == 0) {
        image_labels_[i]->on_resize = [this](const QSize& size) {
          camera_preview_width_.store(
              std::clamp(size.width(), 1, kCameraPreviewWidth),
              std::memory_order_release);
          camera_preview_height_.store(
              std::clamp(size.height(), 1, kCameraPreviewHeight),
              std::memory_order_release);
        };
      }
      frame_labels_[i] = new QLabel(
          uiText("RX complete sets=0 fps=0.00",
                 "接收完整帧组=0 帧率=0.00"),
          tile);
      frame_labels_[i]->setObjectName(QStringLiteral("cameraStats"));
      frame_labels_[i]->setToolTip(uiText(
          "Counts complete four-camera frame sets received and acknowledged. "
          "The low-latency preview may skip obsolete whole frame sets.",
          "统计已接收并确认的四相机完整帧组。低延迟预览可能跳过过时的整组帧。"));

      tile_layout->addWidget(caption);
      tile_layout->addWidget(image_labels_[i], 1);
      tile_layout->addWidget(frame_labels_[i]);
      video_grid->addWidget(tile, i / 2, i % 2);
    }
    camera_splitter->addWidget(video_group);

    auto* camera_tools = new QTabWidget(camera_splitter);
    camera_tools->setObjectName(QStringLiteral("cameraTools"));
    camera_tools->setMinimumWidth(340);
    camera_encoding_panel_ = new CameraEncodingPanel(camera_tools);
    camera_tools->addTab(
        camera_encoding_panel_, uiText("Stream", "相机流"));
    camera_exposure_panel_ = new CameraExposurePanel(camera_tools);
    camera_tools->addTab(
        camera_exposure_panel_, uiText("Exposure", "曝光"));

    auto* metadata_page = new QWidget(camera_tools);
    auto* metadata_layout = new QVBoxLayout(metadata_page);
    metadata_layout->setContentsMargins(8, 8, 8, 8);
    meta_text_ = new QPlainTextEdit(metadata_page);
    meta_text_->setReadOnly(true);
    meta_text_->setMaximumBlockCount(200);
    metadata_layout->addWidget(meta_text_);
    camera_tools->addTab(metadata_page, uiText("Metadata", "元数据"));
    camera_splitter->addWidget(camera_tools);
    camera_splitter->setStretchFactor(0, 4);
    camera_splitter->setStretchFactor(1, 1);
    camera_splitter->setSizes({1040, 360});

    live_camera_zoom_dialog_ = new CameraZoomDialog(this);
    live_camera_zoom_dialog_->on_selected_camera_changed =
        [this](int camera) {
          live_camera_zoom_camera_.store(camera, std::memory_order_release);
        };
    live_camera_zoom_dialog_->on_visibility_changed =
        [this](bool visible) {
          live_camera_zoom_visible_.store(visible, std::memory_order_release);
          updateVisualizationActivity();
        };

    auto* dataset_controls = new QHBoxLayout();
    dataset_open_button_ = new QPushButton(
        uiText("Open Dataset...", "打开数据集..."), dataset_page_);
    dataset_validate_button_ = new QPushButton(
        uiText("Validate Dataset...", "验证数据集..."), dataset_page_);
    dataset_validate_button_->setObjectName(
        QStringLiteral("datasetValidateButton"));
    dataset_validate_button_->setToolTip(uiText(
        "Check manifests, required streams, timestamps, container ranges, "
        "JPEG payloads, and LiDAR record sizes",
        "检查清单、必需数据流、时间戳、容器边界、JPEG 数据和雷达记录长度"));
    dataset_imu_alignment_button_ = new QPushButton(
        uiText("Analyze IMU Offset...", "分析 IMU 时间偏移..."),
        dataset_page_);
    dataset_imu_alignment_button_->setObjectName(
        QStringLiteral("datasetImuAlignmentButton"));
    dataset_imu_alignment_button_->setToolTip(uiText(
        "Estimate the LiDAR IMU timestamp offset relative to onboard IMU0 "
        "from synchronized gyroscope motion",
        "利用同步的陀螺仪运动，估算雷达 IMU 相对板载 IMU0 的时间戳偏移"));
    dataset_export_rosbag_button_ = new QPushButton(
        uiText("Export ROS Bag...", "导出 ROS Bag..."), dataset_page_);
    dataset_export_rosbag_button_->setEnabled(false);
    auto* rosbag_export_menu = new QMenu(dataset_export_rosbag_button_);
    auto* export_ros1_action = rosbag_export_menu->addAction(
        uiText("ROS1 Bag (.bag)", "ROS1 Bag（.bag）"));
    auto* export_ros2_action = rosbag_export_menu->addAction(
        uiText("ROS2 Bag (SQLite3)", "ROS2 Bag（SQLite3）"));
    dataset_export_rosbag_button_->setMenu(rosbag_export_menu);
    connect(export_ros1_action, &QAction::triggered, this,
            [this]() { exportLoadedDatasetRosbag(RosbagFormat::Ros1); });
    connect(export_ros2_action, &QAction::triggered, this,
            [this]() { exportLoadedDatasetRosbag(RosbagFormat::Ros2); });
    dataset_path_label_ = new QLabel(
        uiText("No dataset loaded", "尚未加载数据集"), dataset_page_);
    dataset_path_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    dataset_path_label_->setStyleSheet(QStringLiteral(
        "background: #ffffff; border: 1px solid #d9e2ef; border-radius: 6px;"
        "padding: 7px 10px; color: #344054;"));
    dataset_controls->addWidget(dataset_open_button_);
    dataset_controls->addWidget(dataset_validate_button_);
    dataset_controls->addWidget(dataset_imu_alignment_button_);
    dataset_controls->addWidget(dataset_export_rosbag_button_);
    dataset_controls->addWidget(dataset_path_label_, 1);
    dataset_layout->addLayout(dataset_controls);

    dataset_summary_label_ = new QLabel(
        uiText("Select a Prism dataset directory containing imu0.tum and "
               "imu1.tum (onboard IMUs). Camera indexes are optional for "
               "IMU-only recordings; lidar_imu.tum is detected separately.",
               "请选择包含 imu0.tum 和 imu1.tum 的 Prism 数据集目录；"
               "它们表示板载 IMU。仅 IMU 录制无需相机索引，"
               "lidar_imu.tum 会单独识别为雷达 IMU。"),
        dataset_page_);
    dataset_summary_label_->setWordWrap(true);
    dataset_summary_label_->setStyleSheet(QStringLiteral(
        "background: #f2f4f7; color: #475467; border: 1px solid #d0d5dd;"
        "border-radius: 6px; padding: 8px 10px;"));
    dataset_layout->addWidget(dataset_summary_label_);

    auto* dataset_navigation = new QHBoxLayout();
    dataset_frame_label_ = new QLabel(
        uiText("Frame: -", "帧：-"), dataset_page_);
    dataset_frame_slider_ = new QSlider(Qt::Horizontal, dataset_page_);
    dataset_frame_slider_->setEnabled(false);
    dataset_frame_slider_->setRange(0, 0);
    dataset_navigation->addWidget(dataset_frame_label_);
    dataset_navigation->addWidget(dataset_frame_slider_, 1);
    dataset_layout->addLayout(dataset_navigation);

    auto* dataset_images_group = new QGroupBox(
        uiText("Recorded camera frame set", "已记录的四路相机帧集"),
        dataset_page_);
    auto* dataset_images_grid = new QGridLayout(dataset_images_group);
    dataset_images_grid->setHorizontalSpacing(10);
    dataset_images_grid->setVerticalSpacing(10);
    dataset_images_grid->setColumnStretch(0, 1);
    dataset_images_grid->setColumnStretch(1, 1);
    dataset_images_grid->setRowStretch(0, 1);
    dataset_images_grid->setRowStretch(1, 1);
    for (int camera = 0; camera < 4; ++camera) {
      auto* tile = new QFrame(dataset_images_group);
      tile->setObjectName(QStringLiteral("cameraTile"));
      auto* stack = new QVBoxLayout(tile);
      stack->setContentsMargins(9, 8, 9, 9);
      stack->setSpacing(7);
      auto* dataset_caption = new QLabel(
          uiText("Camera %1", "相机 %1").arg(camera), tile);
      dataset_caption->setObjectName(QStringLiteral("cameraCaption"));
      stack->addWidget(dataset_caption);
      dataset_image_labels_[camera] = new ImageViewLabel(tile);
      dataset_image_labels_[camera]->setObjectName(QStringLiteral("cameraImage"));
      dataset_image_labels_[camera]->setMinimumSize(260, 180);
      dataset_image_labels_[camera]->setCursor(Qt::PointingHandCursor);
      dataset_image_labels_[camera]->clearImage(
          uiText("No dataset frame", "无数据集图像"));
      dataset_image_labels_[camera]->on_click =
          [this, camera]() { showDatasetCameraZoom(camera); };
      stack->addWidget(dataset_image_labels_[camera], 1);
      dataset_images_grid->addWidget(tile, camera / 2, camera % 2);
    }
    dataset_layout->addWidget(dataset_images_group, 1);

    dataset_details_ = new QPlainTextEdit(dataset_page_);
    dataset_details_->setReadOnly(true);
    dataset_details_->setMaximumHeight(120);
    dataset_layout->addWidget(dataset_details_);
    dataset_camera_zoom_dialog_ = new CameraZoomDialog(this);

    QSettings imu_unit_settings(QStringLiteral("DIBULI"),
                                QStringLiteral("PrismViewer"));
    const auto stored_acceleration_unit =
        prism_viewer::imu_units::parseAccelerationUnit(
            imu_unit_settings
                .value(QStringLiteral("imu/acceleration_unit"),
                       unitTokenText(
                           prism_viewer::imu_units::kDefaultAccelerationUnit))
                .toString()
                .toStdString());
    acceleration_unit_ = stored_acceleration_unit.value_or(
        prism_viewer::imu_units::kDefaultAccelerationUnit);
    const auto stored_angular_velocity_unit =
        prism_viewer::imu_units::parseAngularVelocityUnit(
            imu_unit_settings
                .value(
                    QStringLiteral("imu/angular_velocity_unit"),
                    unitTokenText(prism_viewer::imu_units::
                                      kDefaultAngularVelocityUnit))
                .toString()
                .toStdString());
    angular_velocity_unit_ = stored_angular_velocity_unit.value_or(
        prism_viewer::imu_units::kDefaultAngularVelocityUnit);
    const auto stored_temperature_unit =
        prism_viewer::imu_units::parseTemperatureUnit(
            imu_unit_settings
                .value(QStringLiteral("imu/temperature_unit"),
                       unitTokenText(
                           prism_viewer::imu_units::kDefaultTemperatureUnit))
                .toString()
                .toStdString());
    temperature_unit_ = stored_temperature_unit.value_or(
        prism_viewer::imu_units::kDefaultTemperatureUnit);

    imu_table_ = new QTableWidget(2, 13, imu_page_);
    updateImuTableUnitHeaders();
    imu_table_->verticalHeader()->setVisible(false);
    imu_table_->verticalHeader()->setDefaultSectionSize(24);
    imu_table_->horizontalHeader()->setMinimumSectionSize(48);
    imu_table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    imu_table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    imu_table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    imu_table_->horizontalHeader()->setStretchLastSection(true);
    imu_table_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    imu_table_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    imu_table_->setAlternatingRowColors(true);
    imu_table_->setMinimumWidth(0);
    imu_table_->setMinimumHeight(96);
    imu_table_->setMaximumHeight(120);
    for (int row = 0; row < 2; ++row) {
      imu_table_->setItem(row, 0, new QTableWidgetItem(QString::number(row)));
      for (int col = 1; col < imu_table_->columnCount(); ++col) {
        imu_table_->setItem(row, col, new QTableWidgetItem(QStringLiteral("-")));
      }
    }
    auto* imu_group = new QGroupBox(QStringLiteral("IMU"), imu_page_);
    auto* imu_layout = new QVBoxLayout(imu_group);
    auto* imu_plot_controls = new QHBoxLayout();
    imu_plot_controls->addWidget(new QLabel(uiText("Live plot:", "实时曲线："), imu_group));
    imu_selector_group_ = new QButtonGroup(imu_group);
    imu_selector_group_->setExclusive(true);
    imu0_selector_ = new QPushButton(QStringLiteral("IMU 0"), imu_group);
    imu1_selector_ = new QPushButton(QStringLiteral("IMU 1"), imu_group);
    imu0_selector_->setObjectName(QStringLiteral("imuSelectButton"));
    imu1_selector_->setObjectName(QStringLiteral("imuSelectButton"));
    imu0_selector_->setCheckable(true);
    imu1_selector_->setCheckable(true);
    imu_selector_group_->addButton(imu0_selector_, 0);
    imu_selector_group_->addButton(imu1_selector_, 1);
    imu0_selector_->setChecked(true);
    imu_plot_controls->addWidget(imu0_selector_);
    imu_plot_controls->addWidget(imu1_selector_);
    imu_plot_controls->addStretch(1);
    imu_record_status_label_ = new QLabel(
        uiText("Not recording", "未录制"), imu_group);
    imu_record_status_label_->setStyleSheet(QStringLiteral(
        "background: #f2f4f7; color: #475467; border: 1px solid #d0d5dd;"
        "border-radius: 5px; padding: 4px 8px; font-weight: 600;"));
    imu_plot_controls->addWidget(imu_record_status_label_);
    imu_alarm_label_ = new QLabel(
        uiText("Timestamp interval: OK", "时间戳间隔：正常"), imu_group);
    imu_alarm_label_->setStyleSheet(QStringLiteral(
        "background: #ecfdf3; color: #027a48; border: 1px solid #abefc6;"
        "border-radius: 5px; padding: 4px 8px; font-weight: 600;"));
    imu_plot_controls->addWidget(imu_alarm_label_);
    imu_layout->addLayout(imu_plot_controls);

    auto* imu_unit_controls = new QHBoxLayout();
    imu_unit_controls->setSpacing(8);
    imu_unit_controls->addWidget(
        new QLabel(uiText("Acceleration:", "加速度："), imu_group));
    imu_acceleration_unit_selector_ = new QComboBox(imu_group);
    imu_acceleration_unit_selector_->setObjectName(
        QStringLiteral("imuAccelerationUnitCombo"));
    imu_acceleration_unit_selector_->addItem(
        accelerationUnitText(AccelerationUnit::Gravity),
        static_cast<int>(AccelerationUnit::Gravity));
    imu_acceleration_unit_selector_->addItem(
        accelerationUnitText(AccelerationUnit::MetresPerSecondSquared),
        static_cast<int>(AccelerationUnit::MetresPerSecondSquared));
    imu_acceleration_unit_selector_->addItem(
        accelerationUnitText(AccelerationUnit::MilliGravity),
        static_cast<int>(AccelerationUnit::MilliGravity));
    imu_acceleration_unit_selector_->setCurrentIndex(
        imu_acceleration_unit_selector_->findData(
            static_cast<int>(acceleration_unit_)));
    imu_acceleration_unit_selector_->setToolTip(uiText(
        "Choose g, SI acceleration, or milli-g for the live table and plot. "
        "Recorded datasets remain in m/s².",
        "选择实时表格和曲线使用 g、SI 加速度或 mg；录制的数据集仍使用 "
        "m/s²。"));
    imu_unit_controls->addWidget(imu_acceleration_unit_selector_);

    imu_unit_controls->addWidget(
        new QLabel(uiText("Angular rate:", "角速度："), imu_group));
    imu_angular_velocity_unit_selector_ = new QComboBox(imu_group);
    imu_angular_velocity_unit_selector_->setObjectName(
        QStringLiteral("imuAngularVelocityUnitCombo"));
    imu_angular_velocity_unit_selector_->addItem(
        angularVelocityUnitText(AngularVelocityUnit::DegreesPerSecond),
        static_cast<int>(AngularVelocityUnit::DegreesPerSecond));
    imu_angular_velocity_unit_selector_->addItem(
        angularVelocityUnitText(AngularVelocityUnit::RadiansPerSecond),
        static_cast<int>(AngularVelocityUnit::RadiansPerSecond));
    imu_angular_velocity_unit_selector_->addItem(
        angularVelocityUnitText(
            AngularVelocityUnit::MilliDegreesPerSecond),
        static_cast<int>(AngularVelocityUnit::MilliDegreesPerSecond));
    imu_angular_velocity_unit_selector_->setCurrentIndex(
        imu_angular_velocity_unit_selector_->findData(
            static_cast<int>(angular_velocity_unit_)));
    imu_angular_velocity_unit_selector_->setToolTip(uiText(
        "Choose degrees/s, radians/s, or mdps for the live table and plot. "
        "Recorded datasets remain in rad/s.",
        "选择实时表格和曲线使用度/秒、弧度/秒或 mdps；录制的数据集仍使用 "
        "rad/s。"));
    imu_unit_controls->addWidget(imu_angular_velocity_unit_selector_);

    imu_unit_controls->addWidget(
        new QLabel(uiText("Temperature:", "温度："), imu_group));
    imu_temperature_unit_selector_ = new QComboBox(imu_group);
    imu_temperature_unit_selector_->setObjectName(
        QStringLiteral("imuTemperatureUnitCombo"));
    imu_temperature_unit_selector_->addItem(
        temperatureUnitText(TemperatureUnit::Celsius),
        static_cast<int>(TemperatureUnit::Celsius));
    imu_temperature_unit_selector_->addItem(
        temperatureUnitText(TemperatureUnit::MilliCelsius),
        static_cast<int>(TemperatureUnit::MilliCelsius));
    imu_temperature_unit_selector_->setCurrentIndex(
        imu_temperature_unit_selector_->findData(
            static_cast<int>(temperature_unit_)));
    imu_temperature_unit_selector_->setToolTip(uiText(
        "Choose degrees Celsius or milli-degrees Celsius for the live table.",
        "选择实时表格使用摄氏度或毫摄氏度。"));
    imu_unit_controls->addWidget(imu_temperature_unit_selector_);
    imu_unit_controls->addStretch(1);
    imu_layout->addLayout(imu_unit_controls);

    imu_layout->addWidget(imu_table_);
    imu_plot_ = new ImuPlotWidget(imu_group);
    imu_plot_->setUnits(acceleration_unit_, angular_velocity_unit_);
    imu_layout->addWidget(imu_plot_, 1);
    imu_page_layout->addWidget(imu_group, 4);

    log_dialog_ = new QDialog(this);
    log_dialog_->setWindowTitle(uiText("Prism Viewer Log", "Prism Viewer 日志"));
    log_dialog_->setMinimumSize(720, 420);
    log_dialog_->resize(1100, 720);
    auto* log_dialog_layout = new QVBoxLayout(log_dialog_);
    log_dialog_layout->setContentsMargins(12, 12, 12, 12);
    log_dialog_layout->setSpacing(8);
    log_text_ = new QPlainTextEdit(log_dialog_);
    log_text_->setReadOnly(true);
    log_text_->setLineWrapMode(QPlainTextEdit::NoWrap);
    log_text_->setMaximumBlockCount(5000);
    log_text_->setStyleSheet(QStringLiteral(
        "QPlainTextEdit { background: #101828; color: #e4e7ec; border: 1px solid #344054;"
        "font-family: Consolas, 'Courier New', monospace; font-size: 10pt; }"));
    log_dialog_layout->addWidget(log_text_, 1);
    auto* log_actions = new QHBoxLayout();
    log_auto_scroll_ = new QCheckBox(uiText("Auto-scroll", "自动滚动"), log_dialog_);
    log_auto_scroll_->setChecked(true);
    auto* copy_log_button = new QPushButton(uiText("Copy All", "全部复制"), log_dialog_);
    auto* clear_log_button = new QPushButton(uiText("Clear", "清空"), log_dialog_);
    auto* close_log_button = new QPushButton(uiText("Close", "关闭"), log_dialog_);
    log_actions->addWidget(log_auto_scroll_);
    log_actions->addStretch(1);
    log_actions->addWidget(copy_log_button);
    log_actions->addWidget(clear_log_button);
    log_actions->addWidget(close_log_button);
    log_dialog_layout->addLayout(log_actions);

    setCentralWidget(central);

    connect(open_device_button_, &QPushButton::clicked,
            this, [this]() { openDevice(); });
    connect(refresh_devices_button_, &QPushButton::clicked,
            this, [this]() { refreshDeviceList(); });
    connect(language_selector_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
              const QString language = language_selector_->itemData(index).toString();
              const bool requested_chinese = language == QStringLiteral("zh_CN");
              if (requested_chinese ==
                  prism_viewer::common::chineseUi()) {
                return;
              }
              QSettings(QStringLiteral("DIBULI"), QStringLiteral("PrismViewer"))
                  .setValue(QStringLiteral("language"), language);
              QStringList arguments = QCoreApplication::arguments();
              if (!arguments.isEmpty()) arguments.removeFirst();
              QProcess::startDetached(QCoreApplication::applicationFilePath(), arguments);
              QApplication::quit();
            });
    connect(close_device_button_, &QPushButton::clicked,
            this, [this]() { closeDevice(); });
    connect(start_button_, &QPushButton::clicked, this, [this]() { startCapture(); });
    connect(stop_button_, &QPushButton::clicked, this, [this]() { stopCapture(); });
    connect(lidar_enabled_checkbox_, &QCheckBox::toggled,
            this, [this](bool enabled) {
              lidar_model_selector_->setEnabled(
                  enabled && client_.isOpen() && !worker_running_);
              if (!enabled) {
                lidar_status_label_->setText(
                    uiText("LiDAR disabled", "雷达未启用"));
              }
            });
    connect(lidar_point_size_spin_,
            QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int size) {
              lidar_point_cloud_widget_->setPointSize(size);
              QSettings(QStringLiteral("DIBULI"),
                        QStringLiteral("PrismViewer"))
                  .setValue(QStringLiteral("lidar/point_size"), size);
            });
    connect(lidar_network_refresh_button_, &QPushButton::clicked,
            this, [this]() { startLidarNetworkOperation(0); });
    connect(lidar_network_apply_button_, &QPushButton::clicked,
            this, [this]() { startLidarNetworkOperation(1); });
    connect(lidar_network_probe_button_, &QPushButton::clicked,
            this, [this]() { startLidarNetworkOperation(2); });
    connect(host_time_sync_button_, &QPushButton::clicked,
            this, [this]() { startHostTimeSync(); });
    connect(system_upgrade_button_, &QPushButton::clicked,
            this, [this]() { startSystemUpgrade(); });
    wifi_hotspot_panel_->on_refresh =
        [this]() { startWifiHotspotOperation(std::nullopt); };
    wifi_hotspot_panel_->on_enable =
        [this]() { startWifiHotspotOperation(true); };
    wifi_hotspot_panel_->on_disable =
        [this]() { startWifiHotspotOperation(false); };
    device_info_panel_->on_refresh = [this]() { refreshDeviceInfo(); };
    device_info_panel_->on_refresh_versions =
        [this]() { refreshDeviceVersions(); };
    camera_exposure_panel_->on_refresh =
        [this]() { startCameraExposureOperation(std::nullopt); };
    camera_exposure_panel_->on_apply =
        [this](const prism::ExposureConfiguration& configuration) {
          startCameraExposureOperation(configuration);
        };
    camera_encoding_panel_->on_refresh =
        [this]() { startCameraEncodingOperation(std::nullopt); };
    camera_encoding_panel_->on_apply =
        [this](const prism::DeviceConfiguration& configuration) {
          startCameraEncodingOperation(configuration);
        };
    connect(log_button_, &QPushButton::clicked, this, [this]() {
      log_dialog_->show();
      log_dialog_->raise();
      log_dialog_->activateWindow();
    });
    connect(copy_log_button, &QPushButton::clicked, this, [this]() {
      QApplication::clipboard()->setText(log_text_->toPlainText());
    });
    connect(clear_log_button, &QPushButton::clicked,
            log_text_, &QPlainTextEdit::clear);
    connect(close_log_button, &QPushButton::clicked,
            log_dialog_, &QDialog::hide);
    connect(imu_selector_group_, &QButtonGroup::idToggled,
            this, [this](int sensor, bool checked) {
              if (checked && imu_plot_ != nullptr) imu_plot_->setSensor(sensor);
            });
    connect(imu_acceleration_unit_selector_,
            QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int index) {
              if (index < 0) return;
              acceleration_unit_ = static_cast<AccelerationUnit>(
                  imu_acceleration_unit_selector_->itemData(index).toInt());
              QSettings(QStringLiteral("DIBULI"),
                        QStringLiteral("PrismViewer"))
                  .setValue(QStringLiteral("imu/acceleration_unit"),
                            unitTokenText(acceleration_unit_));
              updateImuTableUnitHeaders();
              refreshLatestImuTableValues();
              imu_plot_->setUnits(acceleration_unit_,
                                  angular_velocity_unit_);
            });
    connect(imu_angular_velocity_unit_selector_,
            QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int index) {
              if (index < 0) return;
              angular_velocity_unit_ = static_cast<AngularVelocityUnit>(
                  imu_angular_velocity_unit_selector_->itemData(index)
                      .toInt());
              QSettings(QStringLiteral("DIBULI"),
                        QStringLiteral("PrismViewer"))
                  .setValue(QStringLiteral("imu/angular_velocity_unit"),
                            unitTokenText(angular_velocity_unit_));
              updateImuTableUnitHeaders();
              refreshLatestImuTableValues();
              imu_plot_->setUnits(acceleration_unit_,
                                  angular_velocity_unit_);
            });
    connect(imu_temperature_unit_selector_,
            QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int index) {
              if (index < 0) return;
              temperature_unit_ = static_cast<TemperatureUnit>(
                  imu_temperature_unit_selector_->itemData(index).toInt());
              QSettings(QStringLiteral("DIBULI"),
                        QStringLiteral("PrismViewer"))
                  .setValue(QStringLiteral("imu/temperature_unit"),
                            unitTokenText(temperature_unit_));
              updateImuTableUnitHeaders();
              refreshLatestImuTableValues();
            });
    connect(imu_record_stop_button_, &QPushButton::clicked,
            this, [this]() { stopImuRecording(); });
    connect(dataset_open_button_, &QPushButton::clicked,
            this, [this]() { openRecordedDataset(); });
    connect(dataset_validate_button_, &QPushButton::clicked,
            this, [this]() { validateRecordedDataset(); });
    connect(dataset_imu_alignment_button_, &QPushButton::clicked,
            this, [this]() { analyzeLoadedDatasetImuOffset(); });
    connect(dataset_frame_slider_, &QSlider::valueChanged,
            this, [this](int frame) { showDatasetFrame(frame); });
    connect(tabs_, &QTabWidget::tabBarClicked, this, [this](int index) {
      QWidget* page = tabs_->widget(index);
      if (!client_.isOpen() &&
          (page == camera_page_ || page == imu_page_ || page == lidar_page_ ||
           page == wifi_hotspot_panel_)) {
        const QString section =
            page == camera_page_
                ? uiText("Camera", "相机")
                : (page == imu_page_
                       ? QStringLiteral("IMU")
                       : (page == lidar_page_
                              ? QStringLiteral("LiDAR")
                              : uiText("Network", "网络")));
        showOpenDeviceHint(section);
      }
    });
    connect(tabs_, &QTabWidget::currentChanged, this,
            [this](int) { updateVisualizationActivity(); });

    imu_ui_timer_ = new QTimer(this);
    imu_ui_timer_->setInterval(
        static_cast<int>(kImuUiFlushPeriod.count()));
    imu_ui_timer_->setTimerType(Qt::PreciseTimer);
    connect(imu_ui_timer_, &QTimer::timeout, this,
            [this]() { flushPendingImuUiUpdates(); });
    imu_ui_timer_->start();

    for (auto& worker : camera_preview_workers_) {
      worker = std::thread([this]() { cameraPreviewWorkerMain(); });
    }
    updateVisualizationActivity();
    refreshDeviceList();
  }

  ~MainWindow() override {
    if (live_camera_zoom_dialog_ != nullptr) {
      live_camera_zoom_dialog_->on_selected_camera_changed = {};
      live_camera_zoom_dialog_->on_visibility_changed = {};
    }
    stopWorker();
    stopCameraPreviewWorker();
    dataset_recorder_.stop();
    client_.closeDevice();
  }

 protected:
  void closeEvent(QCloseEvent* event) override {
    stopWorker();
    dataset_recorder_.stop();
    client_.closeDevice();
    event->accept();
  }

  void changeEvent(QEvent* event) override {
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange) {
      updateVisualizationActivity();
    }
  }

 private:
  void updateVisualizationActivity() {
    if (tabs_ == nullptr || camera_page_ == nullptr ||
        imu_page_ == nullptr || lidar_page_ == nullptr) {
      return;
    }
    const bool window_active = !isMinimized();
    const bool camera_active =
        window_active &&
        (tabs_->currentWidget() == camera_page_ ||
         live_camera_zoom_visible_.load(std::memory_order_acquire));
    const bool was_camera_active =
        camera_preview_enabled_.exchange(camera_active,
                                         std::memory_order_acq_rel);
    if (was_camera_active && !camera_active) {
      camera_preview_generation_.fetch_add(1, std::memory_order_acq_rel);
      std::lock_guard<std::mutex> lock(camera_preview_mutex_);
      camera_preview_jobs_.clear();
      camera_preview_completed_.clear();
      camera_preview_next_sequence_ = 1;
    }
    {
      std::lock_guard<std::mutex> lock(imu_ui_mutex_);
      pending_imu_ui_dirty_.fill(false);
      for (auto& samples : pending_imu_plot_samples_) samples.clear();
    }
    const bool imu_active =
        window_active && tabs_->currentWidget() == imu_page_;
    imu_ui_enabled_.store(imu_active, std::memory_order_release);
    if (imu_plot_ != nullptr) imu_plot_->setActive(imu_active);
    lidar_ui_enabled_.store(
        window_active && tabs_->currentWidget() == lidar_page_,
        std::memory_order_release);
  }

  void setStatusAppearance(bool warning) {
    status_label_->setStyleSheet(
        warning
            ? QStringLiteral(
                  "background: #fffaeb; border: 1px solid #fedf89; border-radius: 6px;"
                  "padding: 8px 10px; color: #b54708; font-weight: 600;")
            : QStringLiteral(
                  "background: #ffffff; border: 1px solid #d9e2ef; border-radius: 6px;"
                  "padding: 8px 10px; color: #344054;"));
  }

  void showOpenDeviceHint(const QString& section) {
    setStatusAppearance(true);
    status_label_->setText(
        uiText("%1 unavailable: please open a device first",
               "%1 不可用：请先打开设备").arg(section));
  }

  void startCameraEncodingOperation(
      std::optional<prism::DeviceConfiguration> requested) {
    if (!client_.isOpen()) {
      showOpenDeviceHint(uiText("Camera stream", "相机流"));
      return;
    }
    if (camera_encoding_operation_running_) return;
    if (worker_running_ || time_sync_running_ || wifi_operation_running_ ||
        camera_exposure_operation_running_ || upgrade_running_ ||
        client_.streamTransferActive()) {
      QMessageBox::warning(
          this,
          uiText("Camera stream controls unavailable", "相机流控制不可用"),
          uiText("Stop camera and IMU transfer and wait for the current "
                 "device operation to finish before changing frame rate or "
                 "JPEG quality.",
                 "请先停止相机和 IMU 传输，并等待当前设备操作完成后再修改 "
                 "相机帧率或 JPEG 质量。"));
      return;
    }

    operation_controller_.join();
    camera_encoding_operation_running_ = true;
    camera_encoding_panel_->setBusy(
        true,
        requested.has_value()
            ? uiText("Saving persistent camera stream settings...",
                     "正在保存持久化相机流设置……")
            : uiText("Reading persistent camera stream settings...",
                     "正在读取持久化相机流设置……"));
    refreshControls();

    operation_controller_.start([this, requested]() {
      try {
        prism::DeviceConfiguration configuration =
            client_.deviceConfiguration();
        if (requested.has_value()) {
          configuration.camera_fps = requested->camera_fps;
          configuration.mjpeg_quality = requested->mjpeg_quality;
          configuration = client_.saveDeviceConfiguration(
              configuration, prism::kDeviceConfigFieldCameraFps |
                                 prism::kDeviceConfigFieldMjpegQuality);
        }
        post([this, configuration, requested]() {
          camera_encoding_operation_running_ = false;
          camera_encoding_panel_->setConfiguration(configuration);
          camera_exposure_panel_->setCameraFps(configuration.camera_fps);
          camera_encoding_panel_->setBusy(false);
          if (latest_device_info_valid_) {
            latest_device_info_.camera_fps =
                static_cast<uint16_t>(configuration.camera_fps);
            device_info_panel_->setInfo(latest_device_info_);
            renderDeviceInfoStatus();
          }
          setStatusAppearance(false);
          status_label_->setText(
              requested.has_value()
                  ? uiText("Camera settings saved: %1 FPS · JPEG %2",
                           "相机设置已保存：%1 FPS · JPEG %2")
                        .arg(configuration.camera_fps)
                        .arg(configuration.mjpeg_quality)
                  : uiText("Camera settings refreshed: %1 FPS · JPEG %2",
                           "相机设置已刷新：%1 FPS · JPEG %2")
                        .arg(configuration.camera_fps)
                        .arg(configuration.mjpeg_quality));
          appendLogLine(
              QDateTime::currentDateTime().toString(
                  QStringLiteral("HH:mm:ss.zzz ")) +
              QStringLiteral(
                  "Camera stream settings %1 fps=%2 jpeg_quality=%3 "
                  "generation=%4")
                  .arg(requested.has_value() ? QStringLiteral("saved")
                                             : QStringLiteral("refreshed"))
                  .arg(configuration.camera_fps)
                  .arg(configuration.mjpeg_quality)
                  .arg(configuration.generation));
          refreshControls();
        });
      } catch (const std::exception& ex) {
        const QString error = toQString(ex.what());
        post([this, error]() {
          camera_encoding_operation_running_ = false;
          camera_encoding_panel_->setBusy(false);
          camera_encoding_panel_->setError(error);
          setStatusAppearance(true);
          status_label_->setText(
              uiText("Camera stream configuration failed: %1",
                     "相机流配置失败：%1")
                  .arg(error));
          appendLogLine(
              QDateTime::currentDateTime().toString(
                  QStringLiteral("HH:mm:ss.zzz ")) +
              QStringLiteral("Camera stream configuration failed: %1")
                  .arg(error));
          refreshControls();
        });
      }
    });
  }

  struct CameraExposureOperationRequest {
    bool apply = false;
    prism::ExposureConfiguration configuration;
  };

  void publishCameraExposureResult(
      const prism::ExposureConfiguration& configuration, bool applied) {
    post([this, configuration, applied]() {
      camera_exposure_operation_running_ = false;
      camera_exposure_panel_->setConfiguration(configuration);
      camera_exposure_panel_->setBusy(false);
      setStatusAppearance(false);
      status_label_->setText(
          applied
              ? uiText("Runtime camera exposure applied",
                       "已应用相机运行时曝光设置")
              : uiText("Runtime camera exposure refreshed",
                       "已刷新相机运行时曝光设置"));
      appendLogLine(
          QDateTime::currentDateTime().toString(
              QStringLiteral("HH:mm:ss.zzz ")) +
          QStringLiteral(
              "Camera exposure %1 target=%2 automatic-mask=0x%3 "
              "manual-us=[%4,%5,%6,%7] gain-x1024=[%8,%9,%10,%11]")
              .arg(applied ? QStringLiteral("applied")
                           : QStringLiteral("refreshed"))
              .arg(configuration.target_brightness)
              .arg(configuration.automatic_camera_mask, 2, 16,
                   QLatin1Char('0'))
              .arg(configuration.manual_exposure_time_us[0])
              .arg(configuration.manual_exposure_time_us[1])
              .arg(configuration.manual_exposure_time_us[2])
              .arg(configuration.manual_exposure_time_us[3])
              .arg(configuration.gain_x1024[0])
              .arg(configuration.gain_x1024[1])
              .arg(configuration.gain_x1024[2])
              .arg(configuration.gain_x1024[3]));
      refreshControls();
    });
  }

  void publishCameraExposureError(const QString& error) {
    post([this, error]() {
      camera_exposure_operation_running_ = false;
      camera_exposure_panel_->setBusy(false);
      camera_exposure_panel_->setError(error);
      setStatusAppearance(true);
      status_label_->setText(
          uiText("Camera exposure operation failed: %1",
                 "相机曝光操作失败：%1")
              .arg(error));
      appendLogLine(
          QDateTime::currentDateTime().toString(
              QStringLiteral("HH:mm:ss.zzz ")) +
          QStringLiteral("Camera exposure operation failed: %1").arg(error));
      refreshControls();
    });
  }

  void runCameraExposureOperation(
      const CameraExposureOperationRequest& request) {
    try {
      const prism::ExposureConfiguration result =
          request.apply
              ? client_.setExposureConfiguration(request.configuration)
              : client_.cameraExposure();
      publishCameraExposureResult(result, request.apply);
    } catch (const std::exception& ex) {
      publishCameraExposureError(toQString(ex.what()));
    }
  }

  bool processPendingCameraExposureOperation() {
    std::optional<CameraExposureOperationRequest> request;
    {
      std::lock_guard<std::mutex> lock(camera_exposure_request_mutex_);
      if (!pending_camera_exposure_request_.has_value()) return false;
      request = std::move(pending_camera_exposure_request_);
      pending_camera_exposure_request_.reset();
    }
    runCameraExposureOperation(*request);
    return true;
  }

  void cancelPendingCameraExposureOperation(const QString& reason) {
    bool cancelled = false;
    {
      std::lock_guard<std::mutex> lock(camera_exposure_request_mutex_);
      cancelled = pending_camera_exposure_request_.has_value();
      pending_camera_exposure_request_.reset();
    }
    if (cancelled) publishCameraExposureError(reason);
  }

  void startCameraExposureOperation(
      std::optional<prism::ExposureConfiguration> requested) {
    if (!client_.isOpen()) {
      showOpenDeviceHint(uiText("Camera exposure", "相机曝光"));
      return;
    }
    if (camera_exposure_operation_running_) return;
    if (time_sync_running_ || wifi_operation_running_ ||
        camera_encoding_operation_running_ || upgrade_running_) {
      QMessageBox::warning(
          this,
          uiText("Camera exposure controls unavailable",
                 "相机曝光控制不可用"),
          uiText("Wait for the current device operation to finish. Exposure "
                 "can be changed while normal capture is running.",
                 "请等待当前设备操作完成。正常采集中允许实时修改曝光。"));
      return;
    }

    CameraExposureOperationRequest request;
    request.apply = requested.has_value();
    if (requested.has_value()) request.configuration = *requested;

    camera_exposure_operation_running_ = true;
    camera_exposure_panel_->setBusy(
        true,
        request.apply
            ? uiText("Applying runtime camera exposure...",
                     "正在应用相机运行时曝光设置……")
            : uiText("Reading runtime camera exposure...",
                     "正在读取相机运行时曝光设置……"));
    refreshControls();

    if (worker_running_) {
      {
        std::lock_guard<std::mutex> lock(camera_exposure_request_mutex_);
        pending_camera_exposure_request_ = request;
      }
      appendLog(QStringLiteral(
          "Camera exposure %1 queued on the active capture worker")
                    .arg(request.apply ? QStringLiteral("apply")
                                       : QStringLiteral("refresh")));
      return;
    }

    operation_controller_.join();
    operation_controller_.start(
        [this, request]() { runCameraExposureOperation(request); });
  }

  void startWifiHotspotOperation(
      std::optional<bool> requested_enabled) {
    if (!client_.isOpen()) {
      showOpenDeviceHint(uiText("Network", "网络"));
      return;
    }
    if (worker_running_ || time_sync_running_ ||
        wifi_operation_running_ || camera_exposure_operation_running_ ||
        camera_encoding_operation_running_ ||
        upgrade_running_ ||
        client_.streamTransferActive()) {
      QMessageBox::warning(
          this,
          uiText("Wi-Fi hotspot controls unavailable",
                 "Wi-Fi 热点控制不可用"),
          uiText("Stop camera and IMU transfer and wait for the current "
                 "device operation to finish before changing Wi-Fi hotspot "
                 "settings.",
                 "请先停止相机和 IMU 传输，并等待当前设备操作完成后再更改 "
                 "Wi-Fi 热点设置。"));
      return;
    }
    operation_controller_.join();

    const QString operation =
        !requested_enabled.has_value()
            ? uiText("Reading Wi-Fi hotspot status...",
                     "正在读取 Wi-Fi 热点状态…")
            : (*requested_enabled
                   ? uiText("Enabling Wi-Fi hotspot...",
                            "正在开启 Wi-Fi 热点…")
                   : uiText("Disabling Wi-Fi hotspot...",
                            "正在关闭 Wi-Fi 热点…"));
    wifi_operation_running_ = true;
    wifi_hotspot_panel_->setBusy(true, operation);
    refreshControls();
    appendLog(!requested_enabled.has_value()
                  ? QStringLiteral("Reading Wi-Fi hotspot status")
                  : QStringLiteral("Setting Wi-Fi hotspot enabled=%1")
                        .arg(*requested_enabled ? 1 : 0));

    operation_controller_.start(
        [this, requested_enabled]() {
          try {
            const prism::WifiHotspotStatus status =
                requested_enabled.has_value()
                    ? client_.setWifiHotspotEnabled(*requested_enabled)
                    : client_.wifiHotspotStatus();
            post([this, status, requested_enabled]() {
              wifi_operation_running_ = false;
              wifi_hotspot_panel_->setStatus(
                  toWifiHotspotViewState(status));
              wifi_hotspot_panel_->setBusy(false);
              appendLogLine(
                  QDateTime::currentDateTime().toString(
                      QStringLiteral("HH:mm:ss.zzz ")) +
                  QStringLiteral(
                      "Wi-Fi hotspot status present=%1 enabled=%2 "
                      "ap=%3 dhcp=%4 persisted=%5 interface=%6 "
                      "ssid=%7 error=%8")
                      .arg(status.present ? 1 : 0)
                      .arg(status.enabled ? 1 : 0)
                      .arg(status.ap_running ? 1 : 0)
                      .arg(status.dhcp_running ? 1 : 0)
                      .arg(status.persisted ? 1 : 0)
                      .arg(toQString(status.interface_name))
                      .arg(toQString(status.ssid))
                      .arg(status.error_code));
              if (requested_enabled.has_value()) {
                setStatusAppearance(
                    status.error_code != 0 || !status.error.empty());
                status_label_->setText(
                    status.error_code == 0 && status.error.empty()
                        ? (*requested_enabled
                               ? uiText("Wi-Fi hotspot enabled",
                                        "Wi-Fi 热点已开启")
                               : uiText("Wi-Fi hotspot disabled",
                                        "Wi-Fi 热点已关闭"))
                        : uiText("Wi-Fi hotspot operation reported an error",
                                 "Wi-Fi 热点操作报告错误"));
              }
              refreshControls();
            });
          } catch (const std::exception& ex) {
            const QString error = toQString(ex.what());
            post([this, error]() {
              wifi_operation_running_ = false;
              wifi_hotspot_panel_->setError(error);
              wifi_hotspot_panel_->setBusy(false);
              appendLogLine(
                  QDateTime::currentDateTime().toString(
                      QStringLiteral("HH:mm:ss.zzz ")) +
                  QStringLiteral("Wi-Fi hotspot operation failed: %1")
                      .arg(error));
              refreshControls();
            });
          }
        });
  }

  void refreshDeviceList() {
    if (worker_running_ || wifi_operation_running_ || client_.isOpen()) {
      return;
    }

    const QString previous_serial = device_selector_->currentData().toString();
    device_selector_->clear();
    try {
      device_session_.refresh();
      int restore_index = -1;
      for (size_t i = 0; i < devices_.size(); ++i) {
        const QString serial = wideToQString(devices_[i].serial_number);
        const QString label = serial.isEmpty()
            ? uiText("Device %1 (no serial)", "设备 %1（无序列号）").arg(i + 1)
            : serial;
        device_selector_->addItem(label, serial);
        if (!previous_serial.isEmpty() && serial == previous_serial) {
          restore_index = static_cast<int>(i);
        }
      }
      if (restore_index >= 0) device_selector_->setCurrentIndex(restore_index);
      status_label_->setText(
          devices_.empty()
              ? uiText("No Prism USB device found", "未找到 Prism USB 设备")
              : uiText("Found %1 device(s); select a serial number",
                       "找到 %1 个设备，请选择序列号")
                    .arg(devices_.size()));
      setStatusAppearance(devices_.empty());
      appendLog(QStringLiteral("USB device scan found %1 device(s)")
                    .arg(devices_.size()));
    } catch (const std::exception& ex) {
      setStatusAppearance(true);
      status_label_->setText(
          uiText("Device scan failed: %1", "设备扫描失败：%1").arg(ex.what()));
      appendLog(QStringLiteral("Device scan failed: %1").arg(ex.what()));
    }
    refreshControls();
  }

  void openDevice() {
    if (worker_running_ || wifi_operation_running_ || client_.isOpen()) {
      return;
    }
    operation_controller_.join();

    const int selected = device_selector_->currentIndex();
    if (selected < 0 || selected >= static_cast<int>(devices_.size())) {
      showOpenDeviceHint(uiText("Camera/IMU", "相机/IMU"));
      return;
    }

    setStatusAppearance(false);
    status_label_->setText(uiText("Opening USB device", "正在打开 USB 设备"));
    appendLog(QStringLiteral("Opening USB device through prism_usb_sdk"));
    try {
      const auto opened =
          device_session_.open(static_cast<size_t>(selected));
      const auto& hello = opened.hello;
      const auto& versions = opened.versions;
      const auto& device_info = opened.device_info;
      const auto& configuration = opened.configuration;
      const auto& network = opened.network;
      const QString serial = wideToQString(opened.serial_number);
      appendLog(QStringLiteral("Device serial=%1 path=%2")
                    .arg(serial.isEmpty() ? QStringLiteral("(not reported)") : serial)
                    .arg(wideToQString(opened.path)));
      appendLog(QStringLiteral("Agent %1 %2 protocol=%3")
                    .arg(toQString(hello.app))
                    .arg(toQString(hello.version))
                    .arg(hello.protocol_version));
      appendLog(QStringLiteral("Versions agent=%1 sensor-board=%2")
                    .arg(toQString(versions.agent))
                    .arg(toQString(versions.sensor_board)));
      appendLog(QStringLiteral("Network %1 %2")
                    .arg(toQString(network.primary_interface))
                    .arg(toQString(network.ipv4)));
      appendLog(
          QStringLiteral(
              "DeviceInfo serial=%1 USB=%2 IMUs=%3 cameras=%4 "
              "IMU-fps=%5 camera-fps=%6 WiFi=%7")
              .arg(toQString(device_info.product_serial))
              .arg(QString::fromLatin1(
                  prism_runtime::usbLinkSpeedName(device_info.usb_speed)))
              .arg(device_info.detected_imu_count)
              .arg(device_info.detected_camera_count)
              .arg(device_info.imu_fps)
              .arg(device_info.camera_fps)
              .arg(device_info.wifi.present
                       ? toQString(device_info.wifi.ssid)
                       : QStringLiteral("not-present")));
      updateDeviceInfo(device_info);
      updateDeviceVersions(versions);
      camera_encoding_panel_->setConfiguration(configuration);
      camera_exposure_panel_->setCameraFps(configuration.camera_fps);
      camera_exposure_panel_->setConfiguration(opened.exposure);
      try {
        updateLidarStatus(client_.lidarStatus());
      } catch (const std::exception& lidar_error) {
        appendLog(QStringLiteral("LiDAR status query failed: %1")
                      .arg(lidar_error.what()));
      }
      try {
        updateLidarNetworkStatus(client_.lidarNetworkStatus());
      } catch (const std::exception& lidar_network_error) {
        appendLog(QStringLiteral("LiDAR network status query failed: %1")
                      .arg(lidar_network_error.what()));
      }
      status_label_->setText(
          serial.isEmpty() ? uiText("Device open", "设备已打开")
                           : uiText("Device open: %1", "设备已打开：%1").arg(serial));
    } catch (const std::exception& ex) {
      client_.closeDevice();
      setStatusAppearance(true);
      status_label_->setText(uiText("Open failed: %1", "打开失败：%1").arg(ex.what()));
      appendLog(QStringLiteral("Open device failed: %1").arg(ex.what()));
    }
    refreshControls();
    if (client_.isOpen()) {
      startWifiHotspotOperation(std::nullopt);
    }
  }

  void closeDevice() {
    if (time_sync_running_ || wifi_operation_running_ ||
        camera_exposure_operation_running_ ||
        camera_encoding_operation_running_) {
      return;
    }
    if (worker_running_) {
      appendLog(QStringLiteral("Close requested; stopping active streams first"));
      stopWorker();
    } else if (operation_controller_.joinable()) {
      operation_controller_.join();
    }
    if (dataset_recorder_.isActive()) stopImuRecording();
    if (client_.isOpen()) {
      client_.closeDevice();
      appendLog(QStringLiteral("USB device closed"));
    }
    latest_device_info_valid_ = false;
    latest_device_versions_valid_ = false;
    latest_rk_heartbeat_time_us_ = 0;
    requested_lidar_model_.store(
        static_cast<int>(prism::LidarModel::None),
        std::memory_order_release);
    device_info_panel_->setDeviceOpen(false);
    camera_encoding_panel_->setDeviceOpen(false);
    camera_exposure_panel_->setDeviceOpen(false);
    time_sync_label_->setText(uiText("Time sync: device closed", "时间同步：设备已关闭"));
    host_time_sync_label_->setText(
        uiText("Host/device clock: device closed",
               "主机/设备时钟：设备已关闭"));
    setStatusAppearance(false);
    status_label_->setText(uiText("Device closed", "设备已关闭"));
    wifi_hotspot_panel_->setDeviceOpen(false);
    if (lidar_point_cloud_widget_ != nullptr) {
      lidar_point_cloud_widget_->clearPoints();
    }
    if (lidar_status_label_ != nullptr) {
      lidar_status_label_->setText(
          uiText("LiDAR: device closed", "雷达：设备已关闭"));
    }
    refreshControls();
  }

  void refreshDeviceInfo() {
    if (!client_.isOpen()) {
      showOpenDeviceHint(uiText("Device Info", "设备信息"));
      return;
    }
    if (worker_running_ || time_sync_running_ ||
        wifi_operation_running_ || camera_exposure_operation_running_ ||
        camera_encoding_operation_running_ ||
        upgrade_running_ ||
        client_.streamTransferActive()) {
      return;
    }

    try {
      const auto info = client_.deviceInfo();
      updateDeviceInfo(info);
      appendLog(QStringLiteral("DeviceInfo refreshed"));
    } catch (const std::exception& ex) {
      const QString error = QString::fromUtf8(ex.what());
      if (device_info_panel_ != nullptr) {
        device_info_panel_->setError(error);
      }
      appendLog(QStringLiteral("DeviceInfo refresh failed: %1").arg(error));
    }
  }

  void refreshDeviceVersions() {
    if (!client_.isOpen()) {
      showOpenDeviceHint(uiText("Versions", "版本"));
      return;
    }
    if (worker_running_ || time_sync_running_ ||
        wifi_operation_running_ || camera_exposure_operation_running_ ||
        camera_encoding_operation_running_ ||
        upgrade_running_ ||
        client_.streamTransferActive()) {
      return;
    }

    try {
      const auto versions = client_.deviceVersions();
      updateDeviceVersions(versions);
      appendLog(QStringLiteral("Versions refreshed: agent=%1 sensor-board=%2")
                    .arg(toQString(versions.agent))
                    .arg(toQString(versions.sensor_board)));
    } catch (const std::exception& ex) {
      const QString error = QString::fromUtf8(ex.what());
      if (device_info_panel_ != nullptr) {
        device_info_panel_->setVersionError(error);
      }
      appendLog(QStringLiteral("Version refresh failed: %1").arg(error));
    }
  }

  void startCapture() {
    if (worker_running_ || time_sync_running_ ||
        wifi_operation_running_ || camera_exposure_operation_running_ ||
        camera_encoding_operation_running_ ||
        !client_.isOpen()) {
      if (!client_.isOpen()) {
        showOpenDeviceHint(uiText("Capture", "采集"));
      }
      return;
    }
    prism::LidarModel requested_lidar_model = prism::LidarModel::None;
    if (lidar_enabled_checkbox_->isChecked()) {
      requested_lidar_model = static_cast<prism::LidarModel>(
          lidar_model_selector_->currentData().toInt());
      if (requested_lidar_model != prism::LidarModel::Mid360 &&
          requested_lidar_model != prism::LidarModel::Mid360S) {
        QMessageBox::warning(
            this, uiText("LiDAR model required", "需要选择雷达型号"),
            uiText("Select Mid-360 or Mid-360S before starting capture. "
                   "The Agent will not choose a model automatically.",
                   "开始采集前请选择 Mid-360 或 Mid-360S。Agent 不会自动选择型号。"));
        tabs_->setCurrentWidget(lidar_page_);
        return;
      }
    }
    requested_lidar_model_.store(
        static_cast<int>(requested_lidar_model), std::memory_order_release);
    operation_controller_.join();

    stop_requested_ = false;
    worker_running_ = true;
    resetUi();
    setRunningUi(true);
    setStatusAppearance(true);
    status_label_->setText(
        uiText("Waiting for the RK/sensor-board link before capture",
               "等待 RK 与 sensor-board 连接后再开始采集"));
    appendLog(QStringLiteral(
        "Capture requested; waiting for DeviceInfo to report an online sensor-board before "
        "starting camera or IMU streams"));
    if (requested_lidar_model != prism::LidarModel::None) {
      appendLog(QStringLiteral("LiDAR requested with explicit model=%1")
                    .arg(requested_lidar_model == prism::LidarModel::Mid360
                             ? QStringLiteral("Mid-360")
                             : QStringLiteral("Mid-360S")));
    }

    operation_controller_.start([this]() { workerMain(); });
  }

  void startHostTimeSync() {
    if (!client_.isOpen()) {
      showOpenDeviceHint(uiText("Time synchronization", "时间同步"));
      return;
    }
    if (worker_running_ || time_sync_running_ ||
        wifi_operation_running_ || camera_exposure_operation_running_ ||
        camera_encoding_operation_running_ ||
        client_.streamTransferActive()) {
      QMessageBox::warning(
          this, uiText("Time synchronization unavailable", "无法进行时间同步"),
          uiText("Stop camera and IMU transfer before synchronizing time.",
                 "请先停止相机和 IMU 传输，再进行时间同步。"));
      return;
    }
    operation_controller_.join();

    time_sync_running_ = true;
    host_time_sync_label_->setText(
        uiText("RK clock: measuring offset before setting system time, PHC and RTC...",
               "RK 时钟：正在测量偏差，随后设置系统时间、以太网 PHC 和 RTC……"));
    host_time_sync_label_->setStyleSheet(QStringLiteral(
        "background: #eff8ff; color: #175cd3; border: 1px solid #b2ddff;"
        "border-radius: 6px; padding: 7px 10px; font-weight: 600;"));
    setStatusAppearance(false);
    status_label_->setText(
        uiText("Setting RK system time, Ethernet PHC and RTC while streams are idle",
               "正在空闲状态下设置 RK 系统时间、以太网 PHC 和 RTC"));
    appendLog(QStringLiteral(
        "RK system-time + Ethernet-PHC + RTC synchronization started "
        "(host authoritative, streams idle)"));
    refreshControls();

    operation_controller_.start([this]() {
      try {
        const auto result = client_.synchronizeSystemTime(12, 6, 1000);
        post([this, result]() {
          const QString before_sign = result.before.offset_us >= 0
                                   ? QStringLiteral("+")
                                   : QString();
          const QString after_sign = result.after.offset_us >= 0
                                  ? QStringLiteral("+")
                                  : QString();
          host_time_sync_label_->setText(
              uiText("RK clock synchronized: before=%1%2 us | residual=%3%4 us "
                     "| PHC=%5 | RTC=%6 | passes=%7",
                     "RK 时钟已同步：校准前=%1%2 us | 剩余偏差=%3%4 us "
                     "| PHC=%5 | RTC=%6 | 校准次数=%7")
                  .arg(before_sign)
                  .arg(result.before.offset_us)
                  .arg(after_sign)
                  .arg(result.after.offset_us)
                  .arg(result.ptp_hardware_clock_set
                           ? QStringLiteral("OK")
                           : QStringLiteral("failed"))
                  .arg(toQString(result.rtc_device))
                  .arg(result.correction_passes));
          host_time_sync_label_->setToolTip(
              uiText("The host clock was used as the authority. RK "
                     "CLOCK_REALTIME, the Ethernet PHC and the listed hardware "
                     "RTC were written, then the residual offset was measured again.",
                     "以主机时间为基准，已写入 RK CLOCK_REALTIME、以太网 PHC 和所列硬件 RTC，"
                     "随后重新测量了剩余偏差。"));
          host_time_sync_label_->setStyleSheet(QStringLiteral(
              "background: #ecfdf3; color: #027a48; border: 1px solid #abefc6;"
              "border-radius: 6px; padding: 7px 10px; font-weight: 600;"));
          setStatusAppearance(false);
          status_label_->setText(
              uiText("RK system time, Ethernet PHC and RTC synchronized",
                     "RK 系统时间、以太网 PHC 和 RTC 已同步"));
          appendLogLine(
              QDateTime::currentDateTime().toString(
                  QStringLiteral("HH:mm:ss.zzz ")) +
              QStringLiteral(
                  "RK clock sync complete before=%1 us correction=%2 us "
                  "residual=%3 us PHC=%4 RTC=%5 passes=%6 verified=%7")
                  .arg(result.before.offset_us)
                  .arg(result.applied_correction_us)
                  .arg(result.after.offset_us)
                  .arg(result.ptp_hardware_clock_set ? 1 : 0)
                  .arg(toQString(result.rtc_device))
                  .arg(result.correction_passes)
                  .arg(result.verified ? 1 : 0));
          time_sync_running_ = false;
          refreshControls();
        });
      } catch (const std::exception& ex) {
        const QString error = toQString(ex.what());
        post([this, error]() {
          host_time_sync_label_->setText(
              uiText("Host/device clock: synchronization failed: %1",
                     "主机/设备时钟：同步失败：%1")
                  .arg(error));
          host_time_sync_label_->setStyleSheet(QStringLiteral(
              "background: #fef3f2; color: #b42318; border: 1px solid #fecdca;"
              "border-radius: 6px; padding: 7px 10px; font-weight: 600;"));
          setStatusAppearance(true);
          status_label_->setText(
              uiText("Time synchronization failed", "时间同步失败"));
          appendLogLine(
              QDateTime::currentDateTime().toString(
                  QStringLiteral("HH:mm:ss.zzz ")) +
              QStringLiteral("RK system/RTC sync failed: %1").arg(error));
          time_sync_running_ = false;
          refreshControls();
        });
      }
    });
  }

  void stopCapture() {
    stop_requested_ = true;
    appendLog(QStringLiteral("Stopping streams"));
    /*
     * Let the USB worker begin quiescing camera and IMU immediately. Dataset
     * finalization can otherwise spend seconds draining a slow disk while the
     * board continues to stream and the GUI appears frozen.
     */
    if (dataset_recorder_.isActive()) stopImuRecording();
  }

  void startImuRecording(DatasetRecordingMode mode) {
    try {
      startImuRecordingImpl(mode);
    } catch (const std::exception& exception) {
      const QString detail = toQString(exception.what());
      appendLog(QStringLiteral("Dataset recording start failed: %1")
                    .arg(detail));
      QMessageBox::critical(
          this, uiText("Dataset recording failed", "数据集录制失败"),
          uiText("Unable to start recording: %1", "无法开始录制：%1")
              .arg(detail));
      refreshControls();
    } catch (...) {
      appendLog(QStringLiteral(
          "Dataset recording start failed: unknown exception"));
      QMessageBox::critical(
          this, uiText("Dataset recording failed", "数据集录制失败"),
          uiText("Unable to start recording because of an unexpected error.",
                 "发生意外错误，无法开始录制。"));
      refreshControls();
    }
  }

  void startImuRecordingImpl(DatasetRecordingMode mode) {
    if (!worker_running_ || dataset_recorder_.isActive()) return;

    const bool sensor_board_synced =
        latest_device_info_valid_ &&
        latest_device_info_.sensor_board_time_synced;
    const bool both_onboard_imus_synced =
        latest_device_info_valid_ &&
        (latest_device_info_.imu_time_synced_mask & 0x03u) == 0x03u;
    if (!sensor_board_synced || !both_onboard_imus_synced) {
      const QString detail = uiText(
          "Recording requires the sensor-board and both onboard IMUs to be "
          "synchronized to the RK device time domain. Wait until IMU0 and "
          "IMU1 both show synced before recording.",
          "录制要求 sensor-board 与两路板载 IMU 均同步到 RK 设备时间域。"
          "请等待 IMU0、IMU1 均显示“已同步”后再录制。");
      appendLog(QStringLiteral(
                    "Dataset recording rejected: sensor_board_synced=%1 "
                    "imu_time_synced_mask=0x%2")
                    .arg(sensor_board_synced ? 1 : 0)
                    .arg(latest_device_info_valid_
                             ? latest_device_info_.imu_time_synced_mask
                             : 0u,
                         2, 16, QLatin1Char('0')));
      QMessageBox::warning(
          this, uiText("Time synchronization required", "需要先完成时间同步"),
          detail);
      return;
    }

    const QString selected_directory = QFileDialog::getExistingDirectory(
        this,
        mode == DatasetRecordingMode::ImuOnly
            ? uiText("Select IMU recording directory", "选择 IMU 录制目录")
            : uiText("Select dataset recording directory", "选择数据集录制目录"),
        QDir::homePath(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontUseNativeDialog);
    if (selected_directory.isEmpty()) return;
    /*
     * A modal directory dialog runs a nested event loop. Capture may stop
     * while it is open, so do not create an orphan recording session after
     * the user returns from the dialog.
     */
    if (!worker_running_ || dataset_recorder_.isActive()) {
      appendLog(QStringLiteral(
          "Dataset recording request cancelled because capture stopped"));
      refreshControls();
      return;
    }
    const QDir output_directory(selected_directory);
    bool existing_dataset =
        QFileInfo::exists(
            output_directory.filePath(QStringLiteral("imu0.tum"))) ||
        QFileInfo::exists(
            output_directory.filePath(QStringLiteral("imu1.tum"))) ||
        QFileInfo::exists(
            output_directory.filePath(QStringLiteral("lidar.tum"))) ||
        QFileInfo::exists(
            output_directory.filePath(QStringLiteral("lidar_imu.tum"))) ||
        QFileInfo::exists(
            output_directory.filePath(QStringLiteral("dataset.info"))) ||
        !output_directory.entryList(
             {QStringLiteral("camera-data-*.bin"),
              QStringLiteral("lidar-data-*.bin")},
             QDir::Files)
             .isEmpty();
    for (int camera = 0; camera < 4; ++camera) {
      existing_dataset =
          existing_dataset ||
          QFileInfo::exists(output_directory.filePath(
              QStringLiteral("cam%1.tum").arg(camera))) ||
          QDir(output_directory.filePath(
                   QStringLiteral("cam%1").arg(camera)))
              .exists();
    }
    bool overwrite = false;
    if (existing_dataset) {
      const QString overwrite_message =
          mode == DatasetRecordingMode::ImuOnly
              ? uiText(
                    "This directory already contains a Prism dataset. "
                    "Replace it with an IMU-only recording? Existing camera "
                    "and LiDAR point/IMU data in this directory will be "
                    "removed.\n%1",
                    "该目录已经包含 Prism 数据集。是否替换为仅 IMU 录制？"
                    "目录中已有的相机、雷达点云和雷达 IMU 数据将被删除。\n%1")
                    .arg(selected_directory)
              : uiText(
                    "This directory already contains a Prism dataset. Replace "
                    "onboard IMU, camera, LiDAR point, and LiDAR IMU data "
                    "and indexes?\n%1",
                    "该目录已经包含 Prism 数据集。是否替换 imu0.tum、"
                    "imu1.tum、cam0...cam3 图像、雷达点云、雷达 IMU 数据"
                    "及索引？\n%1")
                    .arg(selected_directory);
      const auto answer = QMessageBox::question(
          this, uiText("Overwrite dataset?", "覆盖数据集？"),
          overwrite_message,
          QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
      if (answer != QMessageBox::Yes) return;
      overwrite = true;
    }

    const auto lidar_model = static_cast<prism::LidarModel>(
        requested_lidar_model_.load(std::memory_order_acquire));
    const bool record_lidar_streams =
        lidar_model != prism::LidarModel::None;
    std::string error;
    if (!dataset_recorder_.start(toFilesystemPath(selected_directory),
                                 overwrite, mode, record_lidar_streams,
                                 &error)) {
      QMessageBox::critical(
          this, uiText("Dataset recording failed", "数据集录制失败"),
          uiText("Unable to start recording: %1", "无法开始录制：%1")
              .arg(toQString(error)));
      return;
    }
    recorded_dataset_root_ = selected_directory;
    if (mode == DatasetRecordingMode::ImuOnly) {
      imu_record_status_label_->setText(
          record_lidar_streams
              ? uiText("Recording onboard IMU0/IMU1 + LiDAR IMU only",
                       "正在仅录制板载 IMU0/IMU1 + 雷达 IMU")
              : uiText("Recording onboard IMU0/IMU1 only",
                       "正在仅录制板载 IMU0/IMU1"));
    } else {
      imu_record_status_label_->setText(
          record_lidar_streams
              ? uiText("Recording 4 cameras + onboard IMU0/IMU1 + "
                       "LiDAR points + LiDAR IMU",
                       "正在录制四路相机 + 板载 IMU0/IMU1 + 雷达点云 + 雷达 IMU")
              : uiText("Recording 4 cameras + onboard IMU0/IMU1",
                       "正在录制四路相机 + 板载 IMU0/IMU1"));
    }
    imu_record_status_label_->setToolTip(selected_directory);
    imu_record_status_label_->setStyleSheet(QStringLiteral(
        "background: #ecfdf3; color: #027a48; border: 1px solid #abefc6;"
        "border-radius: 5px; padding: 4px 8px; font-weight: 700;"));
    appendLog(QStringLiteral(
                  "Dataset recording started: mode=%1 lidar_imu=%2 root=%3")
                  .arg(mode == DatasetRecordingMode::ImuOnly
                           ? QStringLiteral("imu-only")
                           : QStringLiteral("full"))
                  .arg(record_lidar_streams ? 1 : 0)
                  .arg(selected_directory));
    refreshControls();
  }

  void stopImuRecording() {
    const DatasetRecordingSummary summary = dataset_recorder_.stop();
    if (!summary.had_session) return;

    if (summary.success) {
      if (summary.mode == DatasetRecordingMode::ImuOnly) {
        imu_record_status_label_->setText(
            uiText("Saved IMU only: onboard %1/%2, LiDAR %3 samples, "
                   "unsynced dropped %4",
                   "仅 IMU 已保存：板载 %1/%2，雷达 %3 个样本，"
                   "未同步丢弃 %4")
                .arg(summary.sample_count[0])
                .arg(summary.sample_count[1])
                .arg(summary.lidar_imu_sample_count)
                .arg(summary.unsyncedDropCount()));
      } else {
        imu_record_status_label_->setText(
            uiText("Saved: onboard IMU %1/%2, images %3x4, LiDAR %4 "
                   "batches + %5 IMU samples, dropped sets %6, "
                   "unsynced dropped %7",
                   "已保存：板载 IMU %1/%2，图像 %3×4，雷达 %4 批点云 + "
                   "%5 个 IMU 样本，丢弃帧集 %6，未同步丢弃 %7")
                .arg(summary.sample_count[0])
                .arg(summary.sample_count[1])
                .arg(*std::min_element(summary.image_count.begin(),
                                       summary.image_count.end()))
                .arg(summary.lidar_batch_count)
                .arg(summary.lidar_imu_sample_count)
                .arg(summary.dropped_frame_sets)
                .arg(summary.unsyncedDropCount()));
      }
      imu_record_status_label_->setStyleSheet(QStringLiteral(
          "background: #eff8ff; color: #175cd3; border: 1px solid #b2ddff;"
          "border-radius: 5px; padding: 4px 8px; font-weight: 600;"));
      appendLog(QStringLiteral(
                    "Dataset saved: mode=%1 root=%2 imu0=%3 imu1=%4 "
                    "camera_images=%5/%6/%7/%8 dropped_sets=%9 "
                    "lidar_batches=%10 lidar_points=%11 "
                    "dropped_lidar_batches=%12 dropped_lidar_points=%13 "
                    "lidar_imu_samples=%14 unsynced_drops="
                    "imu0:%15/imu1:%16 camera:%17 lidar:%18/%19 "
                    "lidar_imu:%20")
                    .arg(summary.mode == DatasetRecordingMode::ImuOnly
                             ? QStringLiteral("imu-only")
                             : QStringLiteral("full"))
                    .arg(recorded_dataset_root_)
                    .arg(summary.sample_count[0])
                    .arg(summary.sample_count[1])
                    .arg(summary.image_count[0])
                    .arg(summary.image_count[1])
                    .arg(summary.image_count[2])
                    .arg(summary.image_count[3])
                    .arg(summary.dropped_frame_sets)
                    .arg(summary.lidar_batch_count)
                    .arg(summary.lidar_point_count)
                    .arg(summary.dropped_lidar_batches)
                    .arg(summary.dropped_lidar_points)
                    .arg(summary.lidar_imu_sample_count)
                    .arg(summary.unsynced_imu_samples_dropped[0])
                    .arg(summary.unsynced_imu_samples_dropped[1])
                    .arg(summary.unsynced_camera_frame_sets_dropped)
                    .arg(summary.unsynced_lidar_batches_dropped)
                    .arg(summary.unsynced_lidar_points_dropped)
                    .arg(summary.unsynced_lidar_imu_samples_dropped));
      loadRecordedDataset(recorded_dataset_root_, false);
    } else {
      imu_record_status_label_->setText(
          uiText("Recording save failed", "录制保存失败"));
      imu_record_status_label_->setStyleSheet(QStringLiteral(
          "background: #fef3f2; color: #b42318; border: 1px solid #fecdca;"
          "border-radius: 5px; padding: 4px 8px; font-weight: 700;"));
      const QString error = toQString(summary.error);
      appendLog(QStringLiteral("Dataset recording save failed: %1").arg(error));
      QMessageBox::critical(
          this, uiText("Dataset recording failed", "数据集录制失败"), error);
    }
    refreshControls();
  }

  void startSystemUpgrade() {
    if (worker_running_ || wifi_operation_running_ ||
        camera_exposure_operation_running_ ||
        camera_encoding_operation_running_ || !client_.isOpen()) {
      if (!client_.isOpen()) {
        showOpenDeviceHint(uiText("system upgrade", "系统升级"));
      }
      return;
    }
    operation_controller_.join();
    const QString path = QFileDialog::getOpenFileName(
        this,
        uiText("Select Prism system update package", "选择 Prism 系统升级包"),
        QString(),
        uiText("Prism system update (*.zip)",
               "Prism 系统升级包 (*.zip)"),
        nullptr, QFileDialog::DontUseNativeDialog);
    if (path.isEmpty()) return;

    prism::SystemUpgradePackageInfo package;
    try {
      package =
          prism_runtime::inspectSystemUpgradePackage(path.toStdString());
    } catch (const std::exception& ex) {
      QMessageBox::critical(
          this, uiText("Invalid system update package", "系统升级包无效"),
          uiText("The selected ZIP must contain a matching agent image and "
                 "sensor-board BOOT.BIN.\n\n%1",
                 "所选 ZIP 必须同时包含匹配的 agent 镜像和 sensor-board "
                 "BOOT.BIN。\n\n%1")
              .arg(ex.what()));
      return;
    }

    if (QMessageBox::warning(
            this,
            uiText("Upgrade Prism system", "升级 Prism 系统"),
            uiText("Camera and IMU streams must be stopped. This package will "
                   "upgrade and restart the agent first, then upgrade and "
                   "restart the sensor-board after QSPI read-back verification."
                   "\n\nPackage: %1\nAgent: %2\nsensor-board: %3\n\nContinue?",
                   "必须先停止相机和 IMU 数据流。系统会先升级并重启 agent，"
                   "随后在 QSPI 回读验证成功后升级并重启 sensor-board。"
                   "\n\n升级包：%1\nAgent：%2\nsensor-board：%3\n\n是否继续？")
                .arg(toQString(package.package_version))
                .arg(toQString(package.agent_version))
                .arg(toQString(package.sensor_board_version)),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
      return;
    }

    worker_running_ = true;
    upgrade_running_ = true;
    stop_requested_ = false;
    setRunningUi(true);
    stop_button_->setEnabled(false);
    updateStatus(uiText("Validating Prism system update package",
                        "正在验证 Prism 系统升级包"));
    appendLog(
        QStringLiteral("System update package: %1 | package=%2 agent=%3 "
                       "sensor-board=%4")
            .arg(path)
            .arg(toQString(package.package_version))
            .arg(toQString(package.agent_version))
            .arg(toQString(package.sensor_board_version)));
      operation_controller_.start([this, path]() {
      try {
        const auto hello = client_.hello();
        appendLog(QStringLiteral("Connected to %1 %2")
                      .arg(toQString(hello.app)).arg(toQString(hello.version)));
        prism::UpgradeOptions options;
        const auto result = client_.upgradeSystem(
            path.toStdString(), options,
            [this](const prism::SystemUpgradeProgress& status) {
              const unsigned percent = status.total_bytes == 0
                                           ? 0U
                                           : static_cast<unsigned>(
                                                 (status.completed_bytes * 100U) /
                                                 status.total_bytes);
              const QString phase =
                  status.phase == prism::SystemUpgradePhase::Agent
                      ? QStringLiteral("agent")
                      : (status.phase ==
                                 prism::SystemUpgradePhase::SensorBoard
                             ? QStringLiteral("sensor-board")
                             : (status.phase ==
                                        prism::SystemUpgradePhase::Complete
                                    ? QStringLiteral("complete")
                                    : QStringLiteral("validating")));
              updateStatus(QStringLiteral("System upgrade %1% [%2]: %3")
                               .arg(percent)
                               .arg(phase)
                               .arg(toQString(status.message)));
              appendLog(QStringLiteral(
                            "System upgrade phase=%1 component=%2/%3 "
                            "overall=%4/%5: %6")
                            .arg(phase)
                            .arg(status.component_received)
                            .arg(status.component_total)
                            .arg(status.completed_bytes)
                            .arg(status.total_bytes)
                            .arg(toQString(status.message)));
            });
        updateStatus(uiText("Prism system upgrade complete",
                            "Prism 系统升级完成"));
        appendLog(
            QStringLiteral("System upgrade committed: package=%1 agent=%2 "
                           "sensor-board=%3")
                .arg(toQString(result.package.package_version))
                .arg(toQString(result.agent.installed_version))
                .arg(toQString(result.package.sensor_board_version)));
        client_.closeDevice();
        appendLog(QStringLiteral("USB device closed after system upgrade"));
      } catch (const std::exception& ex) {
        updateStatus(uiText("System upgrade failed: %1",
                            "系统升级失败：%1")
                         .arg(ex.what()));
        appendLog(
            QStringLiteral("System upgrade error: %1").arg(ex.what()));
        client_.closeDevice();
      }
      upgrade_running_ = false;
      worker_running_ = false;
      post([this]() {
        refreshControls();
      });
    });
  }

  void stopWorker() {
    stop_requested_ = true;
    operation_controller_.join();
  }

  void resetUi() {
    start_time_ = std::chrono::steady_clock::now();
    camera_preview_generation_.fetch_add(1, std::memory_order_acq_rel);
    {
      std::lock_guard<std::mutex> lock(camera_preview_mutex_);
      camera_preview_jobs_.clear();
      camera_preview_completed_.clear();
      camera_preview_next_sequence_ = 1;
    }
    camera_frames_.fill(0);
    camera_frame_sets_ = 0;
    latest_camera_images_.fill(QImage());
    latest_camera_frame_id_ = 0;
    imu_samples_.fill(0);
    imu_fsync_events_.fill(0);
    imu_timestamp_alarm_.fill(false);
    imu_timestamp_alarm_detail_.fill(QString());
    {
      std::lock_guard<std::mutex> lock(imu_ui_mutex_);
      pending_imu_ui_ = {};
      pending_imu_ui_dirty_.fill(false);
      for (auto& samples : pending_imu_plot_samples_) samples.clear();
    }
    latest_device_info_valid_ = false;
    latest_rk_heartbeat_time_us_ = 0;
    if (imu_alarm_label_ != nullptr) {
      imu_alarm_label_->setText(
          uiText("Timestamp interval: OK", "时间戳间隔：正常"));
      imu_alarm_label_->setStyleSheet(QStringLiteral(
          "background: #ecfdf3; color: #027a48; border: 1px solid #abefc6;"
          "border-radius: 5px; padding: 4px 8px; font-weight: 600;"));
    }
    if (time_sync_label_ != nullptr) {
      time_sync_label_->setText(
          uiText("Time sync: waiting for DeviceInfo", "时间同步：等待 DeviceInfo"));
      time_sync_label_->setStyleSheet(QStringLiteral(
          "background: #f2f4f7; color: #475467; border: 1px solid #d0d5dd;"
          "border-radius: 6px; padding: 7px 10px; font-weight: 600;"));
      time_sync_label_->setToolTip(QString());
    }
    if (host_time_sync_label_ != nullptr) {
      host_time_sync_label_->setText(
          uiText("Host/device clock: not measured",
                 "主机/设备时钟：尚未测量"));
      host_time_sync_label_->setStyleSheet(QStringLiteral(
          "background: #f2f4f7; color: #475467; border: 1px solid #d0d5dd;"
          "border-radius: 6px; padding: 7px 10px; font-weight: 600;"));
      host_time_sync_label_->setToolTip(QString());
    }
    if (imu_plot_ != nullptr) imu_plot_->clear();
    if (lidar_point_cloud_widget_ != nullptr) {
      lidar_point_cloud_widget_->clearPoints();
    }
    if (lidar_status_label_ != nullptr) {
      lidar_status_label_->setText(
          lidar_enabled_checkbox_->isChecked()
              ? uiText("Waiting to start selected LiDAR",
                       "等待启动所选雷达")
              : uiText("LiDAR disabled", "雷达未启用"));
    }
    for (int i = 0; i < 4; ++i) {
      image_labels_[i]->clearImage(uiText("Waiting", "等待数据"));
      frame_labels_[i]->setText(
          uiText("RX complete sets=0 fps=0.00",
                 "接收完整帧组=0 帧率=0.00"));
    }
    for (int row = 0; row < 2; ++row) {
      for (int col = 1; col < imu_table_->columnCount(); ++col) {
        imu_table_->item(row, col)->setText(QStringLiteral("-"));
      }
      imu_table_->item(row, 3)->setBackground(QBrush());
      imu_table_->item(row, 3)->setForeground(QBrush());
    }
    meta_text_->clear();
    log_text_->clear();
  }

  void refreshControls() {
    const bool running = worker_running_;
    const bool time_syncing = time_sync_running_;
    const bool wifi_busy = wifi_operation_running_;
    const bool exposure_busy = camera_exposure_operation_running_;
    const bool encoding_busy = camera_encoding_operation_running_;
    const bool lidar_network_busy = lidar_network_operation_running_;
    const bool busy = running || time_syncing || wifi_busy || exposure_busy ||
                      encoding_busy || lidar_network_busy;
    const bool upgrading = upgrade_running_;
    const bool device_open = client_.isOpen();
    const bool selection_valid = device_selector_->currentIndex() >= 0 &&
        device_selector_->currentIndex() < static_cast<int>(devices_.size());
    device_selector_->setEnabled(!busy && !device_open);
    language_selector_->setEnabled(!busy && !device_open);
    refresh_devices_button_->setEnabled(!busy && !device_open);
    if (camera_page_ != nullptr) camera_page_->setEnabled(device_open);
    if (imu_page_ != nullptr) imu_page_->setEnabled(device_open);
    if (lidar_page_ != nullptr) lidar_page_->setEnabled(device_open);
    if (lidar_enabled_checkbox_ != nullptr) {
      lidar_enabled_checkbox_->setEnabled(device_open && !busy && !upgrading);
    }
    if (lidar_model_selector_ != nullptr) {
      lidar_model_selector_->setEnabled(
          device_open && !busy && !upgrading &&
          lidar_enabled_checkbox_->isChecked());
    }
    const bool lidar_network_controls_enabled =
        device_open && !busy && !upgrading &&
        !client_.streamTransferActive();
    if (lidar_network_enabled_checkbox_ != nullptr) {
      lidar_network_enabled_checkbox_->setEnabled(
          lidar_network_controls_enabled);
    }
    if (lidar_network_host_ip_ != nullptr) {
      lidar_network_host_ip_->setEnabled(lidar_network_controls_enabled);
      lidar_network_netmask_->setEnabled(lidar_network_controls_enabled);
      lidar_network_target_ip_->setEnabled(lidar_network_controls_enabled);
      lidar_network_refresh_button_->setEnabled(
          lidar_network_controls_enabled);
      lidar_network_apply_button_->setEnabled(
          lidar_network_controls_enabled);
      lidar_network_probe_button_->setEnabled(
          lidar_network_controls_enabled);
    }
    if (wifi_hotspot_panel_ != nullptr) {
      wifi_hotspot_panel_->setEnabled(device_open);
      wifi_hotspot_panel_->setDeviceOpen(device_open);
      wifi_hotspot_panel_->setControlsLocked(
          running || time_syncing || exposure_busy || encoding_busy ||
          upgrading ||
          client_.streamTransferActive());
    }
    if (device_info_panel_ != nullptr) {
      device_info_panel_->setDeviceOpen(device_open);
      device_info_panel_->setControlsLocked(
          busy || upgrading || client_.streamTransferActive());
    }
    if (camera_exposure_panel_ != nullptr) {
      camera_exposure_panel_->setDeviceOpen(device_open);
      camera_exposure_panel_->setCaptureActive(running && !upgrading);
      camera_exposure_panel_->setControlsLocked(
          time_syncing || wifi_busy || encoding_busy || upgrading);
    }
    if (camera_encoding_panel_ != nullptr) {
      camera_encoding_panel_->setDeviceOpen(device_open);
      camera_encoding_panel_->setCaptureActive(
          running || client_.streamTransferActive());
      camera_encoding_panel_->setControlsLocked(
          time_syncing || wifi_busy || exposure_busy || upgrading);
    }
    if (imu0_selector_ != nullptr) imu0_selector_->setEnabled(device_open);
    if (imu1_selector_ != nullptr) imu1_selector_->setEnabled(device_open);
    open_device_button_->setEnabled(!busy && !device_open && selection_valid);
    close_device_button_->setEnabled(
        device_open && !upgrading && !time_syncing && !wifi_busy &&
        !exposure_busy && !encoding_busy);
    start_button_->setEnabled(!busy && device_open);
    stop_button_->setEnabled(running);
    host_time_sync_button_->setEnabled(
        !busy && device_open && !client_.streamTransferActive());
    system_upgrade_button_->setEnabled(!busy && device_open);
    const bool imu_recording = dataset_recorder_.isActive();
    if (imu_record_start_button_ != nullptr) {
      imu_record_start_button_->setEnabled(running && !imu_recording);
    }
    if (imu_record_stop_button_ != nullptr) {
      imu_record_stop_button_->setEnabled(imu_recording);
    }
    if (dataset_export_rosbag_button_ != nullptr) {
      dataset_export_rosbag_button_->setEnabled(
          !loaded_dataset_root_.isEmpty() && !imu_recording &&
          !worker_running_ && !rosbag_export_running_);
    }
    if (dataset_validate_button_ != nullptr) {
      dataset_validate_button_->setEnabled(
          !dataset_recorder_.isActive() && !worker_running_ &&
          !rosbag_export_running_);
    }
    if (dataset_imu_alignment_button_ != nullptr) {
      dataset_imu_alignment_button_->setEnabled(
          !loaded_dataset_root_.isEmpty() &&
          dataset_has_imu_alignment_inputs_ &&
          !dataset_recorder_.isActive() && !worker_running_ &&
          !rosbag_export_running_);
    }
  }

  void setRunningUi(bool running) {
    refreshControls();
    status_label_->setText(
        running ? uiText("Running", "正在运行")
                : (client_.isOpen() ? uiText("Device open", "设备已打开")
                                    : uiText("Device closed", "设备已关闭")));
  }

  void post(const std::function<void()>& fn) {
    QMetaObject::invokeMethod(this, fn, Qt::QueuedConnection);
  }

  void appendLog(const QString& text) {
    post([this, text]() {
      appendLogLine(
          QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz ")) +
          text);
    });
  }

  void appendLogLine(const QString& text) {
    log_text_->appendPlainText(text);
    if (log_auto_scroll_ != nullptr && log_auto_scroll_->isChecked()) {
      auto* scroll_bar = log_text_->verticalScrollBar();
      scroll_bar->setValue(scroll_bar->maximum());
    }
  }

  void updateStatus(const QString& text) {
    post([this, text]() {
      setStatusAppearance(false);
      status_label_->setText(text);
    });
  }

  void updateLidarStatus(const prism::LidarStatus& status) {
    post([this, status]() {
      if (lidar_status_label_ == nullptr) return;
      const QString model =
          status.model == prism::LidarModel::Mid360
              ? QStringLiteral("Mid-360")
              : (status.model == prism::LidarModel::Mid360S
                     ? QStringLiteral("Mid-360S")
                     : uiText("not selected", "未选择"));
      QString text;
      QString style;
      if (!status.available) {
        text = uiText("Livox SDK2 is unavailable: %1",
                      "Livox SDK2 不可用：%1")
                   .arg(toQString(status.error));
        style = QStringLiteral(
            "background: #fef3f2; color: #b42318; border: 1px solid #fecdca;"
            "border-radius: 6px; padding: 7px 10px; font-weight: 600;");
      } else if (!status.enabled) {
        text = uiText("LiDAR stopped", "雷达已停止");
        style = QStringLiteral(
            "background: #f2f4f7; color: #475467; border: 1px solid #d0d5dd;"
            "border-radius: 6px; padding: 7px 10px; font-weight: 600;");
      } else if (!status.connected || !status.receiving) {
        const QString detail = status.error.empty()
                                   ? uiText("waiting for Ethernet data",
                                            "等待以太网数据")
                                   : toQString(status.error);
        text = uiText("%1 enabled; %2", "%1 已启用；%2")
                   .arg(model, detail);
        style = QStringLiteral(
            "background: #fffaeb; color: #b54708; border: 1px solid #fedf89;"
            "border-radius: 6px; padding: 7px 10px; font-weight: 600;");
      } else {
        text = uiText("%1 receiving from %2 | points %3 | dropped %4",
                      "%1 正在接收 %2 | 点数 %3 | 丢弃 %4")
                   .arg(model,
                        status.lidar_ip.empty()
                            ? QStringLiteral("-")
                            : toQString(status.lidar_ip))
                   .arg(status.point_count)
                   .arg(status.dropped_point_count);
        style = QStringLiteral(
            "background: #ecfdf3; color: #027a48; border: 1px solid #abefc6;"
            "border-radius: 6px; padding: 7px 10px; font-weight: 600;");
      }
      lidar_status_label_->setText(text);
      lidar_status_label_->setStyleSheet(style);
    });
  }

  void updateLidarNetworkStatus(const prism::LidarNetworkStatus& status) {
    post([this, status]() {
      if (lidar_network_status_label_ == nullptr) return;
      {
        const QSignalBlocker blocker(lidar_network_enabled_checkbox_);
        lidar_network_enabled_checkbox_->setChecked(
            status.configuration.enabled);
      }
      lidar_network_host_ip_->setText(
          toQString(status.configuration.host_ip));
      lidar_network_netmask_->setText(
          toQString(status.configuration.netmask));
      lidar_network_target_ip_->setText(
          toQString(status.configuration.lidar_ip));

      QString text;
      QString style;
      if (!status.configuration.enabled) {
        text = uiText(
                   "%1 disabled | saved generation %2",
                   "%1 已禁用 | 已保存代次 %2")
                   .arg(toQString(status.interface_name))
                   .arg(status.generation);
        style = QStringLiteral(
            "background: #f2f4f7; color: #475467; border: 1px solid #d0d5dd;"
            "border-radius: 6px; padding: 7px 10px; font-weight: 600;");
      } else if (!status.interface_present || !status.link_up ||
                 !status.address_applied || !status.same_subnet ||
                 status.error_code != 0) {
        const QString detail = status.error.empty()
                                   ? uiText("configuration is not active",
                                            "配置尚未生效")
                                   : toQString(status.error);
        text = uiText(
                   "%1 %2/%3 -> Mid360 %4 | %5",
                   "%1 %2/%3 -> Mid360 %4 | %5")
                   .arg(toQString(status.interface_name),
                        toQString(status.configuration.host_ip),
                        toQString(status.configuration.netmask),
                        toQString(status.configuration.lidar_ip), detail);
        style = QStringLiteral(
            "background: #fef3f2; color: #b42318; border: 1px solid #fecdca;"
            "border-radius: 6px; padding: 7px 10px; font-weight: 600;");
      } else if (!status.target_reachable) {
        text = uiText(
                   "Saved | %1 %2 | Mid360 %3",
                   "已保存 | %1 %2 | Mid360 %3")
                   .arg(toQString(status.interface_name),
                        toQString(status.configuration.host_ip),
                        toQString(status.configuration.lidar_ip));
        style = QStringLiteral(
            "background: #f2f4f7; color: #475467; border: 1px solid #d0d5dd;"
            "border-radius: 6px; padding: 7px 10px; font-weight: 600;");
      } else {
        text = uiText(
                   "%1 ready at %2 | Mid360 %3 reachable",
                   "%1 已就绪：%2 | Mid360 %3 可达")
                   .arg(toQString(status.interface_name),
                        toQString(status.configuration.host_ip),
                        toQString(status.configuration.lidar_ip));
        style = QStringLiteral(
            "background: #ecfdf3; color: #027a48; border: 1px solid #abefc6;"
            "border-radius: 6px; padding: 7px 10px; font-weight: 600;");
      }
      lidar_network_status_label_->setText(text);
      lidar_network_status_label_->setStyleSheet(style);
    });
  }

  void startLidarNetworkOperation(int operation) {
    if (!client_.isOpen()) {
      showOpenDeviceHint(QStringLiteral("LiDAR"));
      return;
    }
    if (worker_running_ || time_sync_running_ || wifi_operation_running_ ||
        camera_exposure_operation_running_ ||
        camera_encoding_operation_running_ ||
        lidar_network_operation_running_ || upgrade_running_ ||
        client_.streamTransferActive()) {
      QMessageBox::warning(
          this, uiText("LiDAR network controls unavailable",
                       "LiDAR 网络控制不可用"),
          uiText("Stop camera, IMU, and LiDAR transfer and wait for the "
                 "current device operation to finish.",
                 "请停止相机、IMU 和 LiDAR 传输，并等待当前设备操作完成。"));
      return;
    }
    prism::LidarNetworkConfiguration configuration;
    configuration.enabled = lidar_network_enabled_checkbox_->isChecked();
    configuration.host_ip = lidar_network_host_ip_->text().trimmed().toStdString();
    configuration.netmask = lidar_network_netmask_->text().trimmed().toStdString();
    configuration.lidar_ip =
        lidar_network_target_ip_->text().trimmed().toStdString();

    operation_controller_.join();
    lidar_network_operation_running_ = true;
    lidar_network_status_label_->setText(
        operation == 0
            ? uiText("Reading end0 settings...", "正在读取 end0 设置……")
            : (operation == 1
                   ? uiText("Saving and applying end0 settings...",
                            "正在保存并应用 end0 设置……")
                   : uiText("Testing Mid360 connection...",
                            "正在测试 Mid360 连接……")));
    refreshControls();
    operation_controller_.start([this, operation, configuration]() {
      try {
        const prism::LidarNetworkStatus status =
            operation == 0
                ? client_.lidarNetworkStatus()
                : (operation == 1
                       ? client_.saveLidarNetworkConfiguration(configuration)
                       : client_.probeLidarNetwork());
        post([this, status, operation]() {
          lidar_network_operation_running_ = false;
          updateLidarNetworkStatus(status);
          appendLogLine(
              QDateTime::currentDateTime().toString(
                  QStringLiteral("HH:mm:ss.zzz ")) +
              QStringLiteral(
                  "LiDAR network operation=%1 interface=%2 host=%3 mask=%4 "
                  "target=%5 link=%6 applied=%7 reachable=%8 persisted=%9 error=%10")
                  .arg(operation)
                  .arg(toQString(status.interface_name))
                  .arg(toQString(status.configuration.host_ip))
                  .arg(toQString(status.configuration.netmask))
                  .arg(toQString(status.configuration.lidar_ip))
                  .arg(status.link_up ? 1 : 0)
                  .arg(status.address_applied ? 1 : 0)
                  .arg(status.target_reachable ? 1 : 0)
                  .arg(status.persisted ? 1 : 0)
                  .arg(status.error_code));
          refreshControls();
        });
      } catch (const std::exception& exception) {
        const QString error = toQString(exception.what());
        post([this, error]() {
          lidar_network_operation_running_ = false;
          lidar_network_status_label_->setText(
              uiText("LiDAR network operation failed: %1",
                     "LiDAR 网络操作失败：%1")
                  .arg(error));
          lidar_network_status_label_->setStyleSheet(QStringLiteral(
              "background: #fef3f2; color: #b42318; border: 1px solid #fecdca;"
              "border-radius: 6px; padding: 7px 10px; font-weight: 600;"));
          appendLogLine(
              QDateTime::currentDateTime().toString(
                  QStringLiteral("HH:mm:ss.zzz ")) +
              QStringLiteral("LiDAR network operation failed: %1").arg(error));
          refreshControls();
        });
      }
    });
  }

  void queueLidarPreview(std::vector<prism::LidarPoint> points,
                         prism::LidarModel model, uint64_t total_points,
                         uint32_t batch_id, bool timestamp_synced) {
    if (!lidar_ui_enabled_.load(std::memory_order_acquire)) return;
    post([this, points = std::move(points), model, total_points, batch_id,
          timestamp_synced]() {
      if (lidar_point_cloud_widget_ == nullptr) return;
      lidar_point_cloud_widget_->appendPoints(points);
      const QString model_name = model == prism::LidarModel::Mid360
                                     ? QStringLiteral("Mid-360")
                                     : QStringLiteral("Mid-360S");
      const QString status =
          uiText("%1 point cloud | received %2 | batch %3 | displayed %4",
                 "%1 点云 | 已接收 %2 | 批次 %3 | 显示 %4")
              .arg(model_name)
              .arg(total_points)
              .arg(batch_id)
              .arg(lidar_point_cloud_widget_->pointCount());
      lidar_status_label_->setText(
          timestamp_synced
              ? status + uiText(" | RK time synced", " | RK 时间已同步")
              : status +
                    uiText(" | time unsynced: preview only; recording waits "
                           "for a timestamp-capable Agent",
                           " | 时间未同步：仅预览；录制等待支持时间戳的 Agent"));
      lidar_status_label_->setStyleSheet(
          timestamp_synced
              ? QStringLiteral(
                    "background: #ecfdf3; color: #027a48; border: 1px solid "
                    "#abefc6; border-radius: 6px; padding: 7px 10px; "
                    "font-weight: 600;")
              : QStringLiteral(
                    "background: #fffaeb; color: #b54708; border: 1px solid "
                    "#fedf89; border-radius: 6px; padding: 7px 10px; "
                    "font-weight: 600;"));
    });
  }

  void renderDeviceInfoStatus() {
    if (!latest_device_info_valid_) return;
    const auto& info = latest_device_info_;
    auto imu_text = [&info](size_t sensor) {
      const uint8_t bit = static_cast<uint8_t>(1u << sensor);
      if ((info.imu_present_mask & bit) == 0u)
        return uiText("not seen", "未检测到");
      if ((info.imu_time_synced_mask & bit) != 0u)
        return uiText("synced", "已同步");
      if ((info.imu_receiving_mask & bit) != 0u)
        return uiText("unsynced", "未同步");
      return uiText("idle", "空闲");
    };
    QString rk_time = uiText("RK time waiting for heartbeat",
                             "等待心跳中的 RK 时间");
    if (latest_rk_heartbeat_time_us_ != 0u) {
      rk_time = QDateTime::fromMSecsSinceEpoch(
                    static_cast<qint64>(
                        latest_rk_heartbeat_time_us_ / 1000u))
                    .toUTC()
                    .toString(
                        QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz 'UTC'"));
    }

    QString text;
    QString style;
    if (info.sensor_board_error_flags != 0u) {
      const QString detail =
          info.sensor_board_error.empty()
              ? QString::fromLatin1(prism_runtime::sensorBoardErrorCodeName(
                    info.sensor_board_error_code))
              : toQString(info.sensor_board_error);
      text = uiText(
                 "sensor-board transfer error: %1 | flags=0x%2 | RK %3",
                 "sensor-board 传输错误：%1 | 标志=0x%2 | RK %3")
                 .arg(detail)
                 .arg(info.sensor_board_error_flags, 8, 16,
                      QLatin1Char('0'))
                 .arg(rk_time);
      style = QStringLiteral(
          "background: #fef3f2; color: #b42318; border: 1px solid #fecdca;"
          "border-radius: 6px; padding: 7px 10px; font-weight: 600;");
    } else if (!info.sensor_board_online) {
      text = uiText("Time sync: sensor-board offline | RK %1 | "
                    "IMU0 %2 | IMU1 %3",
                    "时间同步：sensor-board 离线 | RK %1 | "
                    "IMU0 %2 | IMU1 %3")
                 .arg(rk_time, imu_text(0), imu_text(1));
      style = QStringLiteral(
          "background: #fef3f2; color: #b42318; border: 1px solid #fecdca;"
          "border-radius: 6px; padding: 7px 10px; font-weight: 600;");
    } else if (!info.sensor_board_time_synced) {
      text = uiText("Time sync: UNSYNCED | RK %1 | waiting for GPS/NMEA + PPS | "
                    "IMU0 %2 | IMU1 %3",
                    "时间同步：未同步 | RK %1 | 等待 GPS/NMEA + PPS | "
                    "IMU0 %2 | IMU1 %3")
                 .arg(rk_time, imu_text(0), imu_text(1));
      style = QStringLiteral(
          "background: #fffaeb; color: #b54708; border: 1px solid #fedf89;"
          "border-radius: 6px; padding: 7px 10px; font-weight: 600;");
    } else {
      text = uiText("Time sync: SYNCED | RK %1 | IMU0 %2 | IMU1 %3",
                    "时间同步：已同步 | RK %1 | IMU0 %2 | IMU1 %3")
                 .arg(rk_time, imu_text(0), imu_text(1));
      style = QStringLiteral(
          "background: #ecfdf3; color: #027a48; border: 1px solid #abefc6;"
          "border-radius: 6px; padding: 7px 10px; font-weight: 600;");
    }
    time_sync_label_->setText(text);
    time_sync_label_->setStyleSheet(style);
    time_sync_label_->setToolTip(
        QStringLiteral(
            "DeviceInfo: product=%1 USB=%2 IMUs=%3 mask=0x%4 "
            "cameras=%5 mask=0x%6 IMU-fps=%7 camera-fps=%8 "
            "sensor-board-error=0x%9")
            .arg(toQString(info.product_serial))
            .arg(QString::fromLatin1(
                prism_runtime::usbLinkSpeedName(info.usb_speed)))
            .arg(info.detected_imu_count)
            .arg(info.imu_present_mask, 2, 16, QLatin1Char('0'))
            .arg(info.detected_camera_count)
            .arg(info.camera_present_mask, 2, 16, QLatin1Char('0'))
            .arg(info.imu_fps)
            .arg(info.camera_fps)
            .arg(info.sensor_board_error_flags, 8, 16,
                 QLatin1Char('0')));
  }

  void updateDeviceInfo(const prism::DeviceInfo& info) {
    post([this, info]() {
      latest_device_info_ = info;
      latest_device_info_valid_ = true;
      if (device_info_panel_ != nullptr) {
        device_info_panel_->setInfo(info);
      }
      renderDeviceInfoStatus();
    });
  }

  void updateDeviceVersions(const prism::DeviceVersions& versions) {
    post([this, versions]() {
      latest_device_versions_ = versions;
      latest_device_versions_valid_ = true;
      if (device_info_panel_ != nullptr) {
        device_info_panel_->setVersions(versions);
      }
    });
  }

  void updateHeartbeat(const prism::HeartbeatStatus& heartbeat) {
    post([this, heartbeat]() {
      latest_rk_heartbeat_time_us_ = heartbeat.rk_system_time_us;
      renderDeviceInfoStatus();
    });
  }

  void showLiveCameraZoom(int camera) {
    if (camera < 0 || camera >= 4 || latest_camera_images_[camera].isNull()) {
      return;
    }
    live_camera_zoom_camera_.store(camera, std::memory_order_release);
    live_camera_zoom_dialog_->setImageSet(
        latest_camera_images_, camera,
        uiText("Live frame %1", "实时帧 %1").arg(latest_camera_frame_id_));
    live_camera_zoom_dialog_->show();
    live_camera_zoom_dialog_->raise();
    live_camera_zoom_dialog_->activateWindow();
  }

  void showDatasetCameraZoom(int camera) {
    if (camera < 0 || camera >= 4 || dataset_images_[camera].isNull()) return;
    dataset_camera_zoom_dialog_->setImageSet(
        dataset_images_, camera,
        uiText("Dataset frame %1", "数据集帧 %1")
            .arg(dataset_current_frame_ + 1));
    dataset_camera_zoom_dialog_->show();
    dataset_camera_zoom_dialog_->raise();
    dataset_camera_zoom_dialog_->activateWindow();
  }

  QString describeDatasetTimestamp(uint64_t timestamp_us) const {
    // Unix timestamps in the current product are around 1.7e15 us. Smaller
    // values are monotonic device timestamps and must not be rendered as 1970.
    if (timestamp_us >= 100000000000000ULL) {
      return QDateTime::fromMSecsSinceEpoch(
                 static_cast<qint64>(timestamp_us / 1000ULL))
          .toUTC()
          .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz 'UTC'"));
    }
    return QStringLiteral("%1.%2 s (device monotonic)")
        .arg(timestamp_us / 1000000ULL)
        .arg(timestamp_us % 1000000ULL, 6, 10, QLatin1Char('0'));
  }

  void openRecordedDataset() {
    const QString initial =
        loaded_dataset_root_.isEmpty() ? QDir::homePath()
                                       : loaded_dataset_root_;
    const QString directory = QFileDialog::getExistingDirectory(
        this, uiText("Open recorded Prism dataset", "打开已录制的 Prism 数据集"),
        initial,
        QFileDialog::ShowDirsOnly | QFileDialog::DontUseNativeDialog);
    if (!directory.isEmpty()) loadRecordedDataset(directory, true);
  }

  QString describeTimestampValidation(
      const DatasetTimestampSummary& summary) const {
    if (summary.rows == 0u) return uiText("not present", "不存在");
    if (summary.rows == 1u) {
      return uiText("1 record", "1 条记录");
    }
    return uiText(
               "%1 records, median/min/max interval %2 / %3 / %4 us, "
               "discontinuities %5",
               "%1 条记录，中位/最小/最大间隔 %2 / %3 / %4 us，跳变 %5")
        .arg(summary.rows)
        .arg(summary.median_interval_us)
        .arg(summary.minimum_interval_us)
        .arg(summary.maximum_interval_us)
        .arg(summary.discontinuities);
  }

  QString formatDatasetValidationReport(
      const DatasetValidationResult& validation,
      const QString& directory) const {
    QString report =
        uiText("Dataset: %1\nResult: %2\nFormat: %3\nChecked records: "
               "%4\nErrors: %5  Warnings: %6\n\n",
               "数据集：%1\n结果：%2\n格式：%3\n已检查记录：%4\n"
               "错误：%5  警告：%6\n\n")
            .arg(directory)
            .arg(validation.valid
                     ? (validation.warningCount() == 0u
                            ? uiText("VALID", "有效")
                            : uiText("VALID WITH WARNINGS", "有效，但有警告"))
                     : uiText("INVALID", "无效"))
            .arg(validation.format_version == 0u
                     ? uiText("legacy / unspecified", "旧版或未声明")
                     : QStringLiteral("prism-dataset-v%1")
                           .arg(validation.format_version))
            .arg(validation.checked_records)
            .arg(validation.errorCount())
            .arg(validation.warningCount());
    for (size_t camera = 0; camera < validation.cameras.size(); ++camera) {
      report += QStringLiteral("Camera%1: %2\n")
                    .arg(camera)
                    .arg(describeTimestampValidation(
                        validation.cameras[camera]));
    }
    for (size_t imu = 0; imu < validation.onboard_imus.size(); ++imu) {
      report += QStringLiteral("IMU%1: %2\n")
                    .arg(imu)
                    .arg(describeTimestampValidation(
                        validation.onboard_imus[imu]));
    }
    report += QStringLiteral("LiDAR: %1\nLiDAR IMU: %2\n")
                  .arg(describeTimestampValidation(validation.lidar),
                       describeTimestampValidation(validation.lidar_imu));
    if (!validation.issues.empty()) report += QLatin1Char('\n');
    for (const auto& issue : validation.issues) {
      const QString severity =
          issue.severity == DatasetValidationSeverity::Error
              ? uiText("ERROR", "错误")
              : uiText("WARNING", "警告");
      QString location = toQString(issue.file);
      if (issue.line != 0u) {
        location += QStringLiteral(":%1").arg(issue.line);
      }
      if (!location.isEmpty()) location += QStringLiteral(": ");
      report += QStringLiteral("[%1] %2%3\n")
                    .arg(severity, location, toQString(issue.message));
    }
    return report;
  }

  DatasetValidationResult runDatasetValidation(
      const QString& directory, bool show_progress) {
    QProgressDialog progress(
        uiText("Validating dataset...", "正在验证数据集……"),
        uiText("Cancel", "取消"), 0, 0, this);
    progress.setWindowTitle(
        uiText("Validate Dataset", "验证数据集"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(show_progress ? 0 : 1000);
    if (show_progress) progress.show();
    auto result = validatePrismDataset(
        toFilesystemPath(directory),
        [&progress](uint64_t checked, const std::string& file) {
          progress.setLabelText(
              uiText("Checking %1\n%2 records", "正在检查 %1\n%2 条记录")
                  .arg(toQString(file))
                  .arg(checked));
          QApplication::processEvents();
        },
        [&progress]() {
          QApplication::processEvents();
          return progress.wasCanceled();
        });
    progress.close();
    return result;
  }

  void validateRecordedDataset() {
    QString directory = loaded_dataset_root_;
    if (directory.isEmpty()) {
      directory = QFileDialog::getExistingDirectory(
          this, uiText("Select Prism dataset to validate",
                       "选择要验证的 Prism 数据集"),
          QDir::homePath(),
          QFileDialog::ShowDirsOnly | QFileDialog::DontUseNativeDialog);
    }
    if (directory.isEmpty()) return;
    const DatasetValidationResult validation =
        runDatasetValidation(directory, true);
    if (validation.cancelled) return;
    const QString report = formatDatasetValidationReport(validation, directory);
    appendLog(QStringLiteral("Dataset validation %1: %2 errors=%3 warnings=%4")
                  .arg(validation.valid ? QStringLiteral("passed")
                                        : QStringLiteral("failed"),
                       directory)
                  .arg(validation.errorCount())
                  .arg(validation.warningCount()));
    QMessageBox message(this);
    message.setWindowTitle(uiText("Dataset validation", "数据集验证"));
    message.setIcon(!validation.valid
                        ? QMessageBox::Critical
                        : (validation.warningCount() != 0u
                               ? QMessageBox::Warning
                               : QMessageBox::Information));
    message.setText(
        validation.valid
            ? (validation.warningCount() == 0u
                   ? uiText("The dataset is valid.", "数据集有效。")
                   : uiText("The dataset is valid, but timestamp or data "
                            "warnings were found.",
                            "数据集有效，但发现时间戳或数据警告。"))
            : uiText("The dataset is invalid.", "数据集无效。"));
    message.setInformativeText(
        uiText("Checked %1 records. Errors: %2, warnings: %3.",
               "已检查 %1 条记录。错误：%2，警告：%3。")
            .arg(validation.checked_records)
            .arg(validation.errorCount())
            .arg(validation.warningCount()));
    message.setDetailedText(report);
    message.exec();
  }

  void analyzeLoadedDatasetImuOffset() {
    if (loaded_dataset_root_.isEmpty() ||
        !dataset_has_imu_alignment_inputs_ || worker_running_ ||
        rosbag_export_running_ || dataset_recorder_.isActive()) {
      return;
    }
    QProgressDialog progress(
        uiText("Analyzing IMU0 and LiDAR IMU motion...",
               "正在分析 IMU0 与雷达 IMU 的运动……"),
        uiText("Cancel", "取消"), 0, 0, this);
    progress.setWindowTitle(
        uiText("IMU Time Alignment", "IMU 时间对齐"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.show();
    const ImuTimeAlignmentResult result = analyzeImu0LidarImuTimeOffset(
        toFilesystemPath(loaded_dataset_root_),
        [&progress](uint64_t checked, const std::string& file) {
          progress.setLabelText(
              uiText("Analyzing %1\n%2 operations",
                     "正在分析 %1\n%2 次操作")
                  .arg(toQString(file))
                  .arg(checked));
          QApplication::processEvents();
        },
        [&progress]() {
          QApplication::processEvents();
          return progress.wasCanceled();
        });
    progress.close();
    if (result.cancelled) return;

    QString details =
        uiText("Dataset: %1\nIMU0 samples: %2\nLiDAR IMU samples: %3\n"
               "Common RK time: %4 to %5 us\nAnalyzed duration: %6 s\n"
               "Motion standard deviation: %7 deg/s\n"
               "Motion peak-to-peak: %8 deg/s\nCorrelation: %9\n"
               "Correlation peak width: %10 us",
               "数据集：%1\nIMU0 样本：%2\n雷达 IMU 样本：%3\n"
               "共同 RK 时间：%4 至 %5 us\n分析时长：%6 s\n"
               "运动标准差：%7 deg/s\n运动峰峰值：%8 deg/s\n"
               "相关系数：%9\n相关峰宽度：%10 us")
            .arg(loaded_dataset_root_)
            .arg(result.imu0_rows)
            .arg(result.lidar_imu_rows)
            .arg(result.common_start_us)
            .arg(result.common_end_us)
            .arg(result.analyzed_duration_s, 0, 'f', 3)
            .arg(result.motion_stddev_dps, 0, 'f', 3)
            .arg(result.motion_peak_to_peak_dps, 0, 'f', 3)
            .arg(result.correlation, 0, 'f', 6)
            .arg(result.correlation_peak_width_us, 0, 'f', 1);
    QMessageBox message(this);
    message.setWindowTitle(uiText("IMU Time Alignment", "IMU 时间对齐"));
    if (result.valid) {
      message.setIcon(QMessageBox::Information);
      message.setText(
          uiText("LiDAR IMU timestamp offset relative to IMU0: %1 us",
                 "雷达 IMU 相对 IMU0 的时间戳偏移：%1 us")
              .arg(result.offset_us, 0, 'f', 1));
      message.setInformativeText(uiText(
          "Definition: offset = LiDAR IMU timestamp - IMU0 timestamp for "
          "the same motion. Subtract %1 us from LiDAR IMU timestamps to "
          "align them to IMU0. The dataset was not modified.",
          "定义：对于同一运动，偏移 = 雷达 IMU 时间戳 - IMU0 时间戳。"
          "将雷达 IMU 时间戳减去 %1 us 即可对齐到 IMU0。数据集未被修改。")
              .arg(result.offset_us, 0, 'f', 1));
      appendLog(QStringLiteral(
                    "Dataset IMU time alignment: lidar_imu_minus_imu0=%1 us "
                    "correlation=%2 peak_width=%3 us root=%4")
                    .arg(result.offset_us, 0, 'f', 1)
                    .arg(result.correlation, 0, 'f', 6)
                    .arg(result.correlation_peak_width_us, 0, 'f', 1)
                    .arg(loaded_dataset_root_));
    } else {
      message.setIcon(QMessageBox::Warning);
      message.setText(uiText("A reliable IMU time offset could not be "
                             "estimated.",
                             "无法可靠估算 IMU 时间偏移。"));
      const bool insufficient_motion =
          result.status == ImuTimeAlignmentStatus::InsufficientMotion;
      message.setInformativeText(
          insufficient_motion
              ? uiText("Record at least 10 seconds while rotating the whole "
                       "device around multiple axes, then try again. The "
                       "dataset was not modified.",
                       "请在绕多个轴转动整机的同时至少录制 10 秒，然后重试。"
                       "数据集未被修改。")
              : toQString(result.message));
      appendLog(QStringLiteral("Dataset IMU time alignment unavailable: %1 "
                               "root=%2")
                    .arg(toQString(result.message), loaded_dataset_root_));
    }
    details += QStringLiteral("\nstatus: %1").arg(toQString(result.message));
    message.setDetailedText(details);
    message.exec();
  }

  void exportLoadedDatasetRosbag(RosbagFormat format) {
    if (loaded_dataset_root_.isEmpty() || worker_running_ ||
        rosbag_export_running_ ||
        dataset_recorder_.isActive()) {
      return;
    }
    const bool ros2 = format == RosbagFormat::Ros2;
    const QString format_label =
        ros2 ? QStringLiteral("ROS2") : QStringLiteral("ROS1");
    const QFileInfo dataset_info(loaded_dataset_root_);
    const QString suggested =
        dataset_info.dir().filePath(dataset_info.fileName() +
                                    (ros2 ? QStringLiteral(".rosbag2")
                                          : QStringLiteral(".bag")));
    QString output = QFileDialog::getSaveFileName(
        this,
        ros2 ? uiText("Export Prism dataset to ROS2 bag directory",
                      "将 Prism 数据集导出为 ROS2 Bag 目录")
             : uiText("Export Prism dataset to ROS1 bag",
                      "将 Prism 数据集导出为 ROS1 Bag"),
        suggested,
        ros2 ? uiText("ROS2 bag directory (*.rosbag2)",
                      "ROS2 Bag 目录 (*.rosbag2)")
             : uiText("ROS1 bag (*.bag)", "ROS1 Bag (*.bag)"),
        nullptr,
        QFileDialog::DontUseNativeDialog);
    if (output.isEmpty()) return;
    if (QFileInfo(output).suffix().isEmpty()) {
      output += ros2 ? QStringLiteral(".rosbag2") : QStringLiteral(".bag");
    }
    if (worker_running_) {
      appendLog(QStringLiteral(
                    "%1 bag export cancelled because live capture started")
                    .arg(format_label));
      refreshControls();
      return;
    }

    const bool output_exists = QFileInfo::exists(output);
    if (output_exists) {
      const auto answer = QMessageBox::question(
          this, uiText("Replace ROS bag output?", "替换 ROS Bag 输出？"),
          uiText("The output file or directory already exists. Replace it "
                 "after conversion finishes successfully?\n%1",
                 "输出文件或目录已经存在。转换成功后是否替换？\n%1")
              .arg(output),
          QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
      if (answer != QMessageBox::Yes) return;
    }

    QProgressDialog progress(
        ros2 ? uiText("Preparing ROS2 bag...", "正在准备 ROS2 Bag……")
             : uiText("Preparing ROS1 bag...", "正在准备 ROS1 Bag……"),
        uiText("Cancel", "取消"), 0, 1000, this);
    progress.setWindowTitle(
        uiText("Export ROS Bag", "导出 ROS Bag"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setAutoClose(false);
    progress.setAutoReset(false);
    progress.show();

    rosbag_export_running_ = true;
    refreshControls();
    appendLog(QStringLiteral("%1 bag export started: dataset=%2 output=%3")
                  .arg(format_label, loaded_dataset_root_, output));
    const auto result = prism_viewer::dataset::exportDatasetToRosbag(
        toFilesystemPath(loaded_dataset_root_), toFilesystemPath(output),
        format, output_exists,
        [&progress](const prism_viewer::dataset::RosbagExportProgress& state) {
          const uint64_t scaled =
              state.total_records == 0
                  ? 0
                  : std::min<uint64_t>(
                        1000u, state.completed_records * 1000u /
                                   state.total_records);
          progress.setValue(static_cast<int>(scaled));
          progress.setLabelText(
              toQString(state.stage) + QStringLiteral("\n%1 / %2")
                                           .arg(state.completed_records)
                                           .arg(state.total_records));
          QApplication::processEvents();
        },
        [&progress]() {
          QApplication::processEvents();
          return progress.wasCanceled();
        });
    rosbag_export_running_ = false;
    progress.close();
    refreshControls();

    if (result.success) {
      appendLog(
          QStringLiteral("%1 bag export complete: %2 camera=%3 "
                         "onboard_imu=%4 lidar_imu=%5 lidar_batches=%6 "
                         "lidar_points=%7 bytes=%8")
              .arg(format_label)
              .arg(output)
              .arg(result.camera_messages)
              .arg(result.imu_messages)
              .arg(result.lidar_imu_messages)
              .arg(result.lidar_messages)
              .arg(result.lidar_points)
              .arg(result.output_bytes));
      QMessageBox::information(
          this, uiText("ROS bag exported", "ROS Bag 已导出"),
          uiText("Saved %1 bag:\n%2\n\nCamera messages: %3\n"
                 "Onboard IMU messages: %4\nLiDAR IMU messages: %5\n"
                 "LiDAR clouds: %6 (%7 points)",
                 "已保存 %1 Bag：\n%2\n\n相机消息：%3\n板载 IMU 消息：%4\n"
                 "雷达 IMU 消息：%5\nLiDAR 点云：%6 批（%7 点）")
              .arg(format_label)
              .arg(output)
              .arg(result.camera_messages)
              .arg(result.imu_messages)
              .arg(result.lidar_imu_messages)
              .arg(result.lidar_messages)
              .arg(result.lidar_points));
      return;
    }
    if (result.cancelled) {
      appendLog(QStringLiteral("%1 bag export cancelled: %2")
                    .arg(format_label, output));
      return;
    }
    const QString error = toQString(result.error);
    appendLog(QStringLiteral("%1 bag export failed: %2")
                  .arg(format_label, error));
    QMessageBox::critical(
        this, uiText("ROS bag export failed", "ROS Bag 导出失败"), error);
  }

  void loadRecordedDataset(const QString& directory, bool show_errors) {
    const auto root = toFilesystemPath(directory);
    std::array<std::vector<DatasetImageEntry>, 4> camera_entries;
    const TumFileSummary imu0 = summarizeTumFile(root / "imu0.tum");
    const TumFileSummary imu1 = summarizeTumFile(root / "imu1.tum");
    const TumFileSummary lidar = summarizeTumFile(root / "lidar.tum");
    const TumFileSummary lidar_imu = summarizeTumFile(root / "lidar_imu.tum");

    size_t camera_index_count = 0;
    std::string camera_index_error;
    if (!inspectDatasetCameraIndexes(root, camera_entries.size(),
                                     &camera_index_count,
                                     &camera_index_error)) {
      if (show_errors) {
        QMessageBox::critical(
            this, uiText("Unable to open dataset", "无法打开数据集"),
            uiText("Cannot inspect camera indexes in:\n%1\n\n%2",
                   "无法检查以下目录中的相机索引：\n%1\n\n%2")
                .arg(directory, toQString(camera_index_error)));
      }
      return;
    }
    if (camera_index_count != 0 &&
        camera_index_count != camera_entries.size()) {
      if (show_errors) {
        QMessageBox::critical(
            this, uiText("Unable to open dataset", "无法打开数据集"),
            uiText("The dataset has an incomplete set of camera indexes. "
                   "Expected cam0.tum through cam3.tum.\n%1",
                   "数据集中的相机索引不完整，应包含 cam0.tum 到 "
                   "cam3.tum。\n%1")
                .arg(directory));
      }
      return;
    }

    const bool has_cameras = camera_index_count == camera_entries.size();
    std::string error;
    size_t complete_frames = 0;
    if (has_cameras) {
      for (size_t camera = 0; camera < camera_entries.size(); ++camera) {
        if (!loadDatasetImageIndex(root, camera, &camera_entries[camera],
                                   &error)) {
          if (show_errors) {
            QMessageBox::critical(
                this, uiText("Unable to open dataset", "无法打开数据集"),
                uiText("%1\n%2", "%1\n%2")
                    .arg(directory, toQString(error)));
          }
          return;
        }
      }
      complete_frames = camera_entries[0].size();
      for (const auto& entries : camera_entries) {
        complete_frames = std::min(complete_frames, entries.size());
      }
    }

    if (complete_frames == 0 && imu0.rows == 0 && imu1.rows == 0 &&
        lidar.rows == 0 && lidar_imu.rows == 0) {
      if (show_errors) {
        QMessageBox::warning(
            this, uiText("Empty dataset", "空数据集"),
            uiText("No camera, onboard IMU, LiDAR point, or LiDAR IMU "
                   "records were found.",
                   "没有找到相机、板载 IMU、雷达点云或雷达 IMU 数据。"));
      }
      return;
    }

    dataset_camera_entries_ = std::move(camera_entries);
    dataset_frame_count_ = complete_frames;
    loaded_dataset_root_ = directory;
    dataset_has_imu_alignment_inputs_ =
        imu0.rows != 0u && lidar_imu.rows != 0u;
    dataset_path_label_->setText(directory);
    dataset_path_label_->setToolTip(directory);
    if (has_cameras) {
      dataset_summary_label_->setText(
          uiText("Loaded %1 complete four-camera frame sets | onboard "
                 "IMU0 %2 | onboard IMU1 %3 | LiDAR %4 batches | "
                 "LiDAR IMU %5",
                 "已加载 %1 个完整四路帧集 | 板载 IMU0 %2 个样本 | "
                 "板载 IMU1 %3 个样本 | 雷达点云 %4 批 | 雷达 IMU %5 个样本")
              .arg(dataset_frame_count_)
              .arg(imu0.rows)
              .arg(imu1.rows)
              .arg(lidar.rows)
              .arg(lidar_imu.rows));
    } else if (lidar.rows == 0) {
      dataset_summary_label_->setText(
          uiText("Loaded IMU-only dataset | onboard IMU0 %1 | onboard "
                 "IMU1 %2 | LiDAR IMU %3",
                 "已加载仅 IMU 数据集 | 板载 IMU0 %1 个样本 | "
                 "板载 IMU1 %2 个样本 | 雷达 IMU %3 个样本")
              .arg(imu0.rows)
              .arg(imu1.rows)
              .arg(lidar_imu.rows));
    } else {
      dataset_summary_label_->setText(
          uiText("Loaded dataset without cameras | onboard IMU0 %1 | "
                 "onboard IMU1 %2 | LiDAR %3 batches | LiDAR IMU %4",
                 "已加载无相机数据集 | 板载 IMU0 %1 个样本 | "
                 "板载 IMU1 %2 个样本 | 雷达点云 %3 批 | 雷达 IMU %4 个样本")
              .arg(imu0.rows)
              .arg(imu1.rows)
              .arg(lidar.rows)
              .arg(lidar_imu.rows));
    }
    dataset_summary_label_->setStyleSheet(QStringLiteral(
        "background: #ecfdf3; color: #027a48; border: 1px solid #abefc6;"
        "border-radius: 6px; padding: 8px 10px; font-weight: 600;"));

    QString details;
    details += QStringLiteral("root=%1\n").arg(directory);
    details += QStringLiteral(
                   "camera frames: cam0=%1 cam1=%2 cam2=%3 cam3=%4\n")
                   .arg(dataset_camera_entries_[0].size())
                   .arg(dataset_camera_entries_[1].size())
                   .arg(dataset_camera_entries_[2].size())
                   .arg(dataset_camera_entries_[3].size());
    details += QStringLiteral("onboard imu0: samples=%1 first=%2 last=%3\n")
                   .arg(imu0.rows)
                   .arg(imu0.first_timestamp_us)
                   .arg(imu0.last_timestamp_us);
    details += QStringLiteral("onboard imu1: samples=%1 first=%2 last=%3\n")
                   .arg(imu1.rows)
                   .arg(imu1.first_timestamp_us)
                   .arg(imu1.last_timestamp_us);
    details += QStringLiteral("lidar points: batches=%1 first=%2 last=%3\n")
                   .arg(lidar.rows)
                   .arg(lidar.first_timestamp_us)
                   .arg(lidar.last_timestamp_us);
    details += QStringLiteral("lidar imu: samples=%1 first=%2 last=%3")
                   .arg(lidar_imu.rows)
                   .arg(lidar_imu.first_timestamp_us)
                   .arg(lidar_imu.last_timestamp_us);
    dataset_details_->setPlainText(details);

    if (has_cameras && dataset_frame_count_ != 0) {
      const QSignalBlocker blocker(dataset_frame_slider_);
      dataset_frame_slider_->setRange(
          0, static_cast<int>(std::min<size_t>(
                 dataset_frame_count_ - 1,
                 static_cast<size_t>(std::numeric_limits<int>::max()))));
      dataset_frame_slider_->setValue(0);
      dataset_frame_slider_->setEnabled(true);
      showDatasetFrame(0);
    } else {
      const QSignalBlocker blocker(dataset_frame_slider_);
      dataset_frame_slider_->setRange(0, 0);
      dataset_frame_slider_->setValue(0);
      dataset_frame_slider_->setEnabled(false);
      dataset_frame_label_->setText(uiText("Frame: -", "帧：-"));
      for (auto* image_label : dataset_image_labels_) {
        image_label->clearImage(
            has_cameras
                ? uiText("No camera frames were recorded",
                         "没有录制到相机帧")
                : uiText("No camera data (IMU-only dataset)",
                         "无相机数据（仅 IMU 数据集）"));
        image_label->setToolTip(QString());
      }
      dataset_camera_zoom_dialog_->hide();
    }
    appendLog(QStringLiteral(
                  "Recorded dataset loaded: %1 mode=%2 frame_sets=%3")
                  .arg(directory)
                  .arg(has_cameras
                           ? QStringLiteral("full")
                           : (lidar.rows == 0 ? QStringLiteral("imu-only")
                                              : QStringLiteral("no-camera")))
                  .arg(dataset_frame_count_));
    refreshControls();
  }

  void showDatasetFrame(int frame) {
    if (frame < 0 || static_cast<size_t>(frame) >= dataset_frame_count_) return;
    dataset_current_frame_ = frame;
    for (size_t camera = 0; camera < dataset_images_.size(); ++camera) {
      const auto& entry = dataset_camera_entries_[camera][frame];
      QImage image = loadDatasetImage(entry);
      dataset_images_[camera] = image;
      if (image.isNull()) {
        dataset_image_labels_[camera]->clearImage(
            uiText("Image file missing", "图像文件缺失"));
      } else {
        dataset_image_labels_[camera]->setImage(image);
      }
      dataset_image_labels_[camera]->setToolTip(
          uiText("%1 @ %2 + %3 | actual exposure=%4 us",
                 "%1 @ %2 + %3 | 实际曝光=%4 微秒")
              .arg(entry.absolute_path)
              .arg(entry.byte_offset)
              .arg(entry.byte_size)
              .arg(entry.exposure_us));
    }

    const uint64_t timestamp_us =
        dataset_camera_entries_[0][frame].timestamp_us;
    dataset_frame_label_->setText(
        uiText("Frame %1/%2 | %3 | exposure [%4, %5, %6, %7] us",
               "帧 %1/%2 | %3 | 曝光 [%4, %5, %6, %7] 微秒")
            .arg(frame + 1)
            .arg(dataset_frame_count_)
            .arg(describeDatasetTimestamp(timestamp_us))
            .arg(dataset_camera_entries_[0][frame].exposure_us)
            .arg(dataset_camera_entries_[1][frame].exposure_us)
            .arg(dataset_camera_entries_[2][frame].exposure_us)
            .arg(dataset_camera_entries_[3][frame].exposure_us));
    if (dataset_camera_zoom_dialog_->isVisible()) {
      dataset_camera_zoom_dialog_->setImageSet(
          dataset_images_, dataset_camera_zoom_dialog_->selectedCamera(),
          uiText("Dataset frame %1/%2", "数据集帧 %1/%2")
              .arg(frame + 1)
              .arg(dataset_frame_count_));
    }
  }

  void queueCameraFrameSetStatus(
      uint32_t frame_id, uint64_t received_frame_sets, double received_fps,
      const std::array<size_t, 4>& jpeg_sizes,
      const std::array<uint32_t, 4>& exposure_us) {
    const uint64_t generation =
        camera_preview_generation_.load(std::memory_order_acquire);
    post([this, generation, frame_id, received_frame_sets, received_fps,
          jpeg_sizes, exposure_us]() {
      if (generation !=
          camera_preview_generation_.load(std::memory_order_acquire)) {
        return;
      }

      /*
       * These are transport/assembly counters, not preview counters. A complete
       * four-camera frame set has already been assembled and acknowledged
       * before this update is queued. Lossy preview decode/render scheduling
       * must never make one camera appear to have dropped on the link.
       */
      camera_frame_sets_ = received_frame_sets;
      camera_frames_.fill(camera_frame_sets_);
      const QString preview_frame =
          latest_camera_images_[0].isNull()
              ? QStringLiteral("-")
              : QString::number(latest_camera_frame_id_);
      for (size_t camera = 0; camera < frame_labels_.size(); ++camera) {
        frame_labels_[camera]->setText(
            uiText("RX frame=%1 complete sets=%2 fps=%3 preview frame=%4 "
                   "jpeg=%5 KiB exposure=%6 us",
                   "接收帧=%1 完整帧组=%2 帧率=%3 预览帧=%4 "
                   "JPEG=%5 KiB 曝光=%6 微秒")
                .arg(frame_id)
                .arg(camera_frame_sets_)
                .arg(received_fps, 0, 'f', 2)
                .arg(preview_frame)
                .arg(static_cast<double>(jpeg_sizes[camera]) / 1024.0, 0,
                     'f', 1)
                .arg(exposure_us[camera]));
      }
    });
  }

  void enqueueCameraPreview(CameraPreviewJob job) {
    if (!camera_preview_enabled_.load(std::memory_order_acquire)) return;
    {
      std::lock_guard<std::mutex> lock(camera_preview_mutex_);
      /*
       * Preview is deliberately lossy. The USB receive loop has already
       * acknowledged and, when requested, recorded the complete frame set.
       * Keeping stale JPEGs here only lets a temporarily slow Qt renderer
       * consume unbounded memory and makes the whole Viewer appear frozen.
       */
      // Latest-wins keeps presentation latency and CPU bounded. Transport
      // ACK and dataset recording are handled before this lossy queue.
      camera_preview_jobs_.clear();
      camera_preview_jobs_.push_back(std::move(job));
    }
    camera_preview_wakeup_.notify_one();
  }

  void publishDecodedCameraPreview(DecodedCameraPreviewJob decoded) {
    std::vector<DecodedCameraPreviewJob> ready;
    {
      std::lock_guard<std::mutex> lock(camera_preview_mutex_);
      if (decoded.generation !=
          camera_preview_generation_.load(std::memory_order_acquire)) {
        return;
      }
      camera_preview_completed_.emplace(
          decoded.received_frame_sets, std::move(decoded));
      while (!camera_preview_completed_.empty()) {
        auto next = camera_preview_completed_.begin();
        if (next->first < camera_preview_next_sequence_) {
          camera_preview_completed_.erase(next);
          continue;
        }
        /*
         * A bounded preview queue may intentionally skip obsolete frames.
         * Advance across such a gap instead of waiting forever for a preview
         * frame that was already discarded.
         */
        camera_preview_next_sequence_ = next->first;
        ready.push_back(std::move(next->second));
        camera_preview_completed_.erase(next);
        ++camera_preview_next_sequence_;
      }
    }

    for (auto& frame : ready) {
      if (!frame.decode_ok) {
        appendLog(QStringLiteral(
                      "Preview decode skipped for complete four-camera frame "
                      "set %1 (camera %2 JPEG decode failed); transport data "
                      "was already received and acknowledged")
                      .arg(frame.frame_id)
                      .arg(frame.failed_camera));
        continue;
      }

      if (camera_preview_ui_posts_pending_.load(std::memory_order_acquire) >=
          kMaximumQueuedPreviewFrameSets) {
        continue;
      }
      camera_preview_ui_posts_pending_.fetch_add(1,
                                                 std::memory_order_acq_rel);
      post([this, frame = std::move(frame)]() mutable {
        if (frame.generation !=
            camera_preview_generation_.load(std::memory_order_acquire)) {
          camera_preview_ui_posts_pending_.fetch_sub(
              1, std::memory_order_acq_rel);
          return;
        }
        latest_camera_images_ = std::move(frame.images);
        latest_camera_frame_id_ = frame.frame_id;
        for (size_t camera = 0; camera < latest_camera_images_.size();
             ++camera) {
          image_labels_[camera]->setImage(latest_camera_images_[camera]);
        }
        if (live_camera_zoom_dialog_->isVisible()) {
          live_camera_zoom_dialog_->setImageSet(
              latest_camera_images_,
              live_camera_zoom_dialog_->selectedCamera(),
              uiText("Live frame %1", "实时帧 %1").arg(frame.frame_id));
        }
        camera_preview_ui_posts_pending_.fetch_sub(
            1, std::memory_order_acq_rel);
      });
    }
  }

  void cameraPreviewWorkerMain() {
    for (;;) {
      CameraPreviewJob job;
      {
        std::unique_lock<std::mutex> lock(camera_preview_mutex_);
        camera_preview_wakeup_.wait(lock, [this]() {
          return camera_preview_stop_ || !camera_preview_jobs_.empty();
        });
        if (camera_preview_stop_) return;
        job = std::move(camera_preview_jobs_.front());
        camera_preview_jobs_.pop_front();
      }

      DecodedCameraPreviewJob decoded;
      decoded.frame_id = job.frame_id;
      decoded.received_frame_sets = job.received_frame_sets;
      decoded.generation = job.generation;
      if (camera_preview_ui_posts_pending_.load(std::memory_order_acquire) >=
          kMaximumQueuedPreviewFrameSets) {
        continue;
      }
      for (size_t camera = 0; camera < decoded.images.size(); ++camera) {
        decoded.images[camera] = decodePreviewJpeg(
            job.jpeg[camera],
            static_cast<int>(camera) == job.full_resolution_camera
                ? QSize()
                : job.maximum_preview_size);
        if (decoded.images[camera].isNull()) {
          decoded.decode_ok = false;
          decoded.failed_camera = camera;
          break;
        }
      }
      publishDecodedCameraPreview(std::move(decoded));
    }
  }

  void stopCameraPreviewWorker() {
    {
      std::lock_guard<std::mutex> lock(camera_preview_mutex_);
      camera_preview_stop_ = true;
      camera_preview_jobs_.clear();
      camera_preview_completed_.clear();
    }
    camera_preview_wakeup_.notify_all();
    for (auto& worker : camera_preview_workers_) {
      if (worker.joinable()) worker.join();
    }
  }

  void processFrameSet(
      uint32_t frame_id,
      uint64_t timestamp_us,
      uint64_t received_frame_sets,
      const prism::VideoMeta& metadata,
      std::array<std::vector<uint8_t>, 4> jpeg_set) {
    dataset_recorder_.appendFrameSet(
        frame_id, timestamp_us, metadata, jpeg_set);

    if (!camera_preview_enabled_.load(std::memory_order_acquire)) return;

    CameraPreviewJob preview;
    preview.frame_id = frame_id;
    preview.received_frame_sets = received_frame_sets;
    preview.generation =
        camera_preview_generation_.load(std::memory_order_acquire);
    preview.full_resolution_camera =
        live_camera_zoom_visible_.load(std::memory_order_acquire)
            ? live_camera_zoom_camera_.load(std::memory_order_acquire)
            : -1;
    preview.maximum_preview_size = QSize(
        camera_preview_width_.load(std::memory_order_acquire),
        camera_preview_height_.load(std::memory_order_acquire));
    preview.jpeg = std::move(jpeg_set);
    enqueueCameraPreview(std::move(preview));
  }

  void updateMeta(const prism::VideoMeta& meta) {
    post([this, meta]() {
      QString text;
      text += QStringLiteral("valid=%1 cameras=%2\n")
                  .arg(meta.valid ? 1 : 0)
                  .arg(meta.cameras);
      text += QStringLiteral("host_frame_id=%1 carrier_frame_id=%2\n")
                  .arg(meta.host_frame_id)
                  .arg(meta.carrier_frame_id);
      text += QStringLiteral("carrier_width_bytes=%1 image_height_per_camera=%2 meta_row_bytes=%3\n")
                  .arg(meta.carrier_width_bytes)
                  .arg(meta.image_height_per_camera)
                  .arg(meta.meta_row_bytes);
      text += QStringLiteral("trigger_time_ns=%1\n")
                  .arg(meta.trigger_time_ns);
      for (int i = 0; i < 4; ++i) {
        text += uiText("camera%1 actual exposure=%2 us\n",
                       "相机%1 实际曝光=%2 微秒\n")
                    .arg(i)
                    .arg(meta.exposure_us[i]);
      }
      text += QStringLiteral("meta_crc32=0x%1\n")
                  .arg(meta.meta_crc32, 8, 16, QLatin1Char('0'));
      meta_text_->setPlainText(text);
    });
  }

  void queueImuUiUpdate(const prism::ImuSample& sample,
                        uint64_t received_count,
                        double sample_rate_hz,
                        uint64_t fsync_event_count,
                        uint64_t last_fsync_sample_us,
                        bool last_fsync_delay_valid) {
    const int sensor = static_cast<int>(sample.sensor_id);
    if (sensor < 0 || sensor >= 2) return;

    ImuUiSnapshot snapshot;
    snapshot.sample = sample;
    snapshot.received_count = received_count;
    snapshot.sample_rate_hz = sample_rate_hz;
    snapshot.fsync_event_count = fsync_event_count;
    snapshot.last_fsync_sample_us = last_fsync_sample_us;
    snapshot.last_fsync_delay_valid = last_fsync_delay_valid;
    std::lock_guard<std::mutex> lock(imu_ui_mutex_);
    pending_imu_ui_[sensor] = std::move(snapshot);
    pending_imu_ui_dirty_[sensor] = true;
  }

  void updateImuTableUnitHeaders() {
    if (imu_table_ == nullptr) return;
    const QString acceleration_unit = accelerationUnitText(acceleration_unit_);
    const QString angular_velocity_unit =
        angularVelocityUnitText(angular_velocity_unit_);
    const QString temperature_unit = temperatureUnitText(temperature_unit_);
    imu_table_->setHorizontalHeaderLabels({
        QStringLiteral("IMU"),
        uiText("Samples", "样本数"),
        QStringLiteral("Hz"),
        uiText("Timestamp", "时间戳"),
        QStringLiteral("Ax ") + acceleration_unit,
        QStringLiteral("Ay ") + acceleration_unit,
        QStringLiteral("Az ") + acceleration_unit,
        QStringLiteral("Gx ") + angular_velocity_unit,
        QStringLiteral("Gy ") + angular_velocity_unit,
        QStringLiteral("Gz ") + angular_velocity_unit,
        uiText("Temp %1", "温度 %1").arg(temperature_unit),
        QStringLiteral("FSYNC"),
        uiText("Flags", "标志"),
    });
  }

  void queueImuPlotSample(
      const prism::ImuSample& sample,
      std::chrono::steady_clock::time_point received_at) {
    if (!imu_ui_enabled_.load(std::memory_order_acquire) ||
        sample.sensor_id >= pending_imu_plot_samples_.size()) {
      return;
    }
    std::lock_guard<std::mutex> lock(imu_ui_mutex_);
    auto& samples = pending_imu_plot_samples_[sample.sensor_id];
    if (samples.size() >= kMaximumPendingImuPlotSamples) {
      samples.pop_front();
    }
    samples.push_back({sample, received_at});
  }

  void updateImuMeasurementCells(const prism::ImuSample& sample) {
    const int sensor = static_cast<int>(sample.sensor_id);
    if (sensor < 0 || sensor >= imu_table_->rowCount()) return;
    for (size_t axis = 0; axis < 3; ++axis) {
      const double acceleration = convertAcceleration(
          static_cast<double>(sample.accel_mg[axis]),
          AccelerationUnit::MilliGravity, acceleration_unit_);
      imu_table_->item(sensor, 4 + static_cast<int>(axis))
          ->setText(QString::number(
              acceleration, 'f',
              accelerationDisplayPrecision(acceleration_unit_)));
      const double angular_velocity = convertAngularVelocity(
          static_cast<double>(sample.gyro_mdps[axis]),
          AngularVelocityUnit::MilliDegreesPerSecond,
          angular_velocity_unit_);
      imu_table_->item(sensor, 7 + static_cast<int>(axis))
          ->setText(QString::number(
              angular_velocity, 'f',
              angularVelocityDisplayPrecision(angular_velocity_unit_)));
    }
    imu_table_->item(sensor, 10)->setText(QString::number(
        convertTemperature(static_cast<double>(sample.temp_milli_c),
                           TemperatureUnit::MilliCelsius, temperature_unit_),
        'f', temperatureDisplayPrecision(temperature_unit_)));
  }

  void applyImuUiSnapshot(const ImuUiSnapshot& snapshot) {
      const prism::ImuSample& sample = snapshot.sample;
      const int sensor = static_cast<int>(sample.sensor_id);
      const uint64_t received_count = snapshot.received_count;
      const double sample_rate_hz = snapshot.sample_rate_hz;
      const uint64_t fsync_event_count = snapshot.fsync_event_count;
      const uint64_t last_fsync_sample_us =
          snapshot.last_fsync_sample_us;
      const bool last_fsync_delay_valid =
          snapshot.last_fsync_delay_valid;
      imu_samples_[sensor] = received_count;
      imu_table_->item(sensor, 1)->setText(QString::number(imu_samples_[sensor]));
      imu_table_->item(sensor, 2)->setText(QString::number(sample_rate_hz, 'f', 1));
      const bool timestamp_synced = sample.timestamp_synced;
      QString timestamp_text;
      if (timestamp_synced) {
        timestamp_text = QDateTime::fromMSecsSinceEpoch(
                             static_cast<qint64>(sample.timestamp_us / 1000u))
                             .toUTC()
                             .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz 'UTC'"));
      } else {
        timestamp_text = uiText("%1 us (unsynced)", "%1 us（未同步）")
                             .arg(sample.timestamp_us);
      }
      imu_table_->item(sensor, 3)->setText(timestamp_text);
      imu_table_->item(sensor, 3)->setToolTip(
          QStringLiteral("raw timestamp: %1 us").arg(sample.timestamp_us));
      updateImuMeasurementCells(sample);
      if (fsync_event_count == 0) {
        imu_table_->item(sensor, 11)->setText(uiText("waiting", "等待中"));
      } else {
        imu_table_->item(sensor, 11)->setText(
            QStringLiteral("#%1 UTC=%2 us %3")
                .arg(fsync_event_count)
                .arg(last_fsync_sample_us)
                .arg(last_fsync_delay_valid ? uiText("valid", "有效")
                                            : uiText("delay invalid", "延迟无效")));
        imu_table_->item(sensor, 11)->setToolTip(
            QStringLiteral("tagged sample UTC: %1 us").arg(last_fsync_sample_us));
      }
      if (fsync_event_count != imu_fsync_events_[sensor]) {
        imu_fsync_events_[sensor] = fsync_event_count;
        appendLogLine(
            QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz ")) +
            QStringLiteral("IMU%1 FSYNC #%2 tagged_sample_utc=%3 us "
                           "delay=%4")
                .arg(sensor)
                .arg(fsync_event_count)
                .arg(last_fsync_sample_us)
                .arg(last_fsync_delay_valid ? QStringLiteral("valid")
                                            : QStringLiteral("invalid")));
      }
      imu_table_->item(sensor, 12)->setText(
          QStringLiteral("0x%1").arg(sample.flags, 4, 16, QLatin1Char('0')));
  }

  void refreshLatestImuTableValues() {
    std::array<ImuUiSnapshot, 2> snapshots;
    std::array<bool, 2> valid{};
    {
      std::lock_guard<std::mutex> lock(imu_ui_mutex_);
      snapshots = pending_imu_ui_;
      for (size_t sensor = 0; sensor < snapshots.size(); ++sensor) {
        valid[sensor] = snapshots[sensor].received_count != 0;
      }
    }
    if (!valid[0] && !valid[1]) return;

    imu_table_->setUpdatesEnabled(false);
    for (size_t sensor = 0; sensor < snapshots.size(); ++sensor) {
      if (valid[sensor]) updateImuMeasurementCells(snapshots[sensor].sample);
    }
    imu_table_->setUpdatesEnabled(true);
    imu_table_->viewport()->update();
  }

  void flushPendingImuUiUpdates() {
    if (!imu_ui_enabled_.load(std::memory_order_acquire)) return;

    std::array<ImuUiSnapshot, 2> snapshots;
    std::array<bool, 2> dirty{};
    std::array<std::deque<PendingImuPlotSample>, 2> plot_samples;
    {
      std::lock_guard<std::mutex> lock(imu_ui_mutex_);
      snapshots = pending_imu_ui_;
      dirty = pending_imu_ui_dirty_;
      pending_imu_ui_dirty_.fill(false);
      for (size_t sensor = 0; sensor < plot_samples.size(); ++sensor) {
        plot_samples[sensor].swap(pending_imu_plot_samples_[sensor]);
      }
    }
    if (!dirty[0] && !dirty[1] && plot_samples[0].empty() &&
        plot_samples[1].empty()) {
      return;
    }

    if (dirty[0] || dirty[1]) {
      imu_table_->setUpdatesEnabled(false);
      for (size_t sensor = 0; sensor < dirty.size(); ++sensor) {
        if (dirty[sensor]) applyImuUiSnapshot(snapshots[sensor]);
      }
      imu_table_->setUpdatesEnabled(true);
      imu_table_->viewport()->update();
    }
    for (const auto& sensor_samples : plot_samples) {
      for (const auto& pending : sensor_samples) {
        imu_plot_->appendSample(pending.sample, pending.received_at);
      }
    }
  }

  void updateImuTimestampAlarm(int sensor, bool active, const QString& detail) {
    post([this, sensor, active, detail]() {
      if (sensor < 0 || sensor >= static_cast<int>(imu_timestamp_alarm_.size())) return;
      if (imu_timestamp_alarm_[sensor] == active) return;

      imu_timestamp_alarm_[sensor] = active;
      imu_timestamp_alarm_detail_[sensor] = active ? detail : QString();
      auto* timestamp_item = imu_table_->item(sensor, 3);
      if (active) {
        timestamp_item->setBackground(QColor(QStringLiteral("#fee4e2")));
        timestamp_item->setForeground(QColor(QStringLiteral("#b42318")));
      } else {
        timestamp_item->setBackground(QBrush());
        timestamp_item->setForeground(QBrush());
      }

      QStringList active_alarms;
      for (size_t i = 0; i < imu_timestamp_alarm_.size(); ++i) {
        if (imu_timestamp_alarm_[i]) {
          active_alarms.push_back(QStringLiteral("IMU%1: %2")
                                      .arg(i)
                                      .arg(imu_timestamp_alarm_detail_[i]));
        }
      }
      if (active_alarms.empty()) {
        imu_alarm_label_->setText(
            uiText("Timestamp interval: OK", "时间戳间隔：正常"));
        imu_alarm_label_->setStyleSheet(QStringLiteral(
            "background: #ecfdf3; color: #027a48; border: 1px solid #abefc6;"
            "border-radius: 5px; padding: 4px 8px; font-weight: 600;"));
      } else {
        imu_alarm_label_->setText(uiText("TIMESTAMP ALARM | ", "时间戳告警 | ") +
                                  active_alarms.join(QStringLiteral(" | ")));
        imu_alarm_label_->setStyleSheet(QStringLiteral(
            "background: #fee4e2; color: #b42318; border: 1px solid #fda29b;"
            "border-radius: 5px; padding: 4px 8px; font-weight: 700;"));
      }

      appendLogLine(
          QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz ")) +
          (active ? QStringLiteral("ALARM: IMU%1 timestamp interval %2").arg(sensor).arg(detail)
                  : QStringLiteral("RECOVERED: IMU%1 timestamp interval stable").arg(sensor)));
    });
  }

  double elapsedSeconds() const {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start_time_).count();
  }

  void workerMain() {
    bool video_started = false;
    bool aggregate_stream_start_attempted = false;
    bool aggregate_stream_stop_attempted = false;
    bool camera_progress_stalled = false;
    try {
      if (!client_.isOpen()) {
        throw std::runtime_error("device is not open");
      }

      // The USB connection only proves that Viewer can reach the RK agent.
      // DeviceInfo is the status interface; heartbeat contains RK time only.
      bool sensor_board_link_ready = false;
      std::optional<prism::DeviceInfo> capture_device_info;
      std::optional<std::chrono::steady_clock::time_point>
          capture_device_info_at;
      while (!stop_requested_ && !sensor_board_link_ready) {
        processPendingCameraExposureOperation();
        try {
          const auto info = client_.deviceInfo();
          capture_device_info = info;
          capture_device_info_at = std::chrono::steady_clock::now();
          updateDeviceInfo(info);
          sensor_board_link_ready = info.sensor_board_online;
        } catch (const std::exception&) {
        }
        if (!stop_requested_ && !sensor_board_link_ready) {
          std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
      }

      if (!sensor_board_link_ready) {
        appendLog(QStringLiteral(
            "Capture cancelled while waiting for the RK/sensor-board link"));
        updateStatus(uiText("Capture not started: waiting was cancelled",
                            "未开始采集：已取消等待 RK 与 sensor-board 连接"));
        cancelPendingCameraExposureOperation(
            uiText("Capture stopped before the exposure request was processed",
                   "采集已停止，曝光请求尚未执行"));
        worker_running_ = false;
        post([this]() { refreshControls(); });
        return;
      }

      appendLog(QStringLiteral(
          "RK/sensor-board link is online; starting camera and IMU streams"));
      updateStatus(uiText("RK/sensor-board link online; starting capture",
                          "RK 与 sensor-board 已连接，正在开始采集"));

      /*
       * Camera and IMU form one capture transaction.  A camera failure must
       * abort the operation instead of silently leaving an IMU-only stream.
       * Passing fps=0 (the SDK default) makes the Agent use the persistent
       * camera_fps selected in the Stream panel.
       */
      aggregate_stream_start_attempted = true;
      const auto status = client_.startVideo1280x1024();
      video_started = true;
      appendLog(QStringLiteral("Video started cameras=%1 fps=%2 size=%3x%4")
                    .arg(status.cameras)
                    .arg(status.fps)
                    .arg(status.width)
                    .arg(status.height));

      prism_viewer::transfer::CameraFrameAssembler camera_assembler;
      std::deque<std::chrono::steady_clock::time_point>
          camera_rate_samples;
      uint64_t received_camera_frame_sets = 0;
      auto last_completed_camera_frame_set_at =
          std::chrono::steady_clock::now();
      auto last_usb_frame_at = last_completed_camera_frame_set_at;
      auto last_video_chunk_at = last_completed_camera_frame_set_at;
      std::optional<uint32_t> last_completed_camera_frame_id;
      std::optional<uint32_t> last_video_chunk_frame_id;
      uint64_t received_video_chunks = 0;
      uint64_t discarded_incomplete_camera_frame_sets = 0;
      std::chrono::steady_clock::time_point next_camera_status_post;
      auto handleCompletedCameraFrame =
          [this, &camera_rate_samples, &received_camera_frame_sets,
           &last_completed_camera_frame_set_at,
           &last_completed_camera_frame_id, &next_camera_status_post](
              prism_viewer::transfer::CameraFrameSet completed) {
            const auto received_at = std::chrono::steady_clock::now();
            last_completed_camera_frame_set_at = received_at;
            last_completed_camera_frame_id = completed.frame_id;
            camera_rate_samples.push_back(received_at);
            while (camera_rate_samples.size() > 2 &&
                   received_at - camera_rate_samples.front() >
                       kCameraRateWindow) {
              camera_rate_samples.pop_front();
            }
            const double camera_elapsed =
                camera_rate_samples.size() > 1
                    ? std::chrono::duration<double>(
                          camera_rate_samples.back() -
                          camera_rate_samples.front())
                          .count()
                    : 0.0;
            const double received_camera_fps =
                camera_elapsed > 0.0
                    ? static_cast<double>(camera_rate_samples.size() - 1) /
                          camera_elapsed
                    : 0.0;
            ++received_camera_frame_sets;

            /*
             * Return flow-control credit once the four JPEGs and their exact
             * per-frame metadata are both available in memory.
             */
            client_.sendVideoAck(completed.frame_id);
            if (received_at >= next_camera_status_post) {
              next_camera_status_post =
                  received_at + kCameraStatusUiPeriod;
              std::array<size_t, 4> jpeg_sizes{};
              for (size_t camera = 0; camera < jpeg_sizes.size(); ++camera) {
                jpeg_sizes[camera] = completed.jpeg[camera].size();
              }
              queueCameraFrameSetStatus(
                  completed.frame_id, received_camera_frame_sets,
                  received_camera_fps, jpeg_sizes,
                  completed.metadata.exposure_us);
            }
            processFrameSet(
                completed.frame_id, completed.timestamp_us,
                received_camera_frame_sets, completed.metadata,
                std::move(completed.jpeg));
          };
      std::array<std::chrono::steady_clock::time_point, 2> last_imu_post{};
      last_imu_post.fill(std::chrono::steady_clock::now());
      std::array<std::chrono::steady_clock::time_point, 2>
          last_imu_plot_post{};
      last_imu_plot_post.fill(std::chrono::steady_clock::now());
      auto next_metadata_ui_update = std::chrono::steady_clock::now();
      std::array<SampleRateTracker, 2> imu_rate_samples;
      std::array<uint64_t, 2> received_imu_samples{};
      std::array<uint64_t, 2> received_fsync_events{};
      std::array<uint64_t, 2> last_fsync_sample_us{};
      std::array<bool, 2> last_fsync_delay_valid{};
      struct TimestampCheck {
        bool initialized = false;
        bool alarm = false;
        bool last_timestamp_synced = false;
        uint64_t last_timestamp_us = 0;
        uint16_t last_sequence = 0;
        uint32_t bad_streak = 0;
        uint32_t good_streak = 0;
      };
      std::array<TimestampCheck, 2> timestamp_checks{};
      const prism::LidarModel requested_lidar_model =
          static_cast<prism::LidarModel>(requested_lidar_model_.load(
              std::memory_order_acquire));
      std::vector<prism::LidarPoint> pending_lidar_preview;
      pending_lidar_preview.reserve(8192u);
      uint64_t received_lidar_points = 0;
      auto last_lidar_preview_post = std::chrono::steady_clock::now();
      prism_runtime::ImuStream imu_stream(
          client_, [this, &last_imu_post, &last_imu_plot_post,
                   &imu_rate_samples, &received_imu_samples,
                   &received_fsync_events, &last_fsync_sample_us,
                   &last_fsync_delay_valid,
                   &timestamp_checks](
                      const prism::ImuSample& sample) {
            const int sensor = static_cast<int>(sample.sensor_id);
            if (sensor < 0 || sensor >= static_cast<int>(received_imu_samples.size())) {
              return;
            }
            dataset_recorder_.appendImu(sample);
            const uint64_t received_count = ++received_imu_samples[sensor];
            if (sample.fsync_event) {
              ++received_fsync_events[sensor];
              last_fsync_sample_us[sensor] = sample.timestamp_us;
              last_fsync_delay_valid[sensor] = sample.fsync_delay_valid;
            }
            const auto now = std::chrono::steady_clock::now();
            auto& rate_tracker = imu_rate_samples[sensor];
            rate_tracker.add(now);
            if (now - last_imu_plot_post[sensor] >=
                kImuPlotSamplePeriod) {
              last_imu_plot_post[sensor] = now;
              queueImuPlotSample(sample, now);
            }

            auto& timestamp_check = timestamp_checks[sensor];
            const uint16_t current_sequence =
                static_cast<uint16_t>(sample.sample_id & 0xffffu);
            const bool timestamp_valid = sample.timestamp_us != 0;
            const bool timestamp_domain_changed =
                timestamp_check.initialized &&
                sample.timestamp_synced != timestamp_check.last_timestamp_synced;
            const uint16_t sequence_delta = timestamp_check.initialized
                                                ? static_cast<uint16_t>(
                                                      current_sequence -
                                                      timestamp_check.last_sequence)
                                                : 0u;
            const bool expected_fsync_reanchor =
                prism_viewer::shouldRebaselineForSyncedFsync(
                    timestamp_check.initialized, sample.timestamp_synced,
                    sample.fsync_event, sample.fsync_delay_valid,
                    sample.sample_gap, sequence_delta) ||
                prism_viewer::shouldRebaselineForFirstUtcFsync(
                    timestamp_check.initialized, sample.timestamp_synced,
                    sample.fsync_event, sample.fsync_delay_valid,
                    sample.sample_gap, sequence_delta,
                    timestamp_check.last_timestamp_us, sample.timestamp_us);

            // Local sensor-board time and synchronized UTC are different time
            // domains.  Every valid synchronized FSYNC sample can replace the
            // extrapolated sample clock with the precise PPS anchor.  When the
            // sequence is continuous and PL reports no sample gap, that clock
            // correction is not a sampling-interval failure.  Establish a
            // fresh baseline and check again from the following sample.
            if (!timestamp_valid || timestamp_domain_changed ||
                expected_fsync_reanchor) {
              timestamp_check.bad_streak = 0;
              // An expected periodic re-anchor must not conceal an alarm
              // raised by an actual earlier stream fault, nor interrupt its
              // run of good samples toward recovery.
              if (!expected_fsync_reanchor) {
                timestamp_check.good_streak = 0;
                if (timestamp_check.alarm) {
                  timestamp_check.alarm = false;
                  updateImuTimestampAlarm(sensor, false, QString());
                }
              }
            } else if (timestamp_check.initialized) {
              const bool timestamp_regressed =
                  sample.timestamp_us <= timestamp_check.last_timestamp_us;
              const bool sequence_repeated = sequence_delta == 0;
              const uint64_t total_delta_us = timestamp_regressed
                                            ? 0
                                            : sample.timestamp_us -
                                                  timestamp_check.last_timestamp_us;
              const uint64_t interval_us = sequence_repeated
                                               ? 0
                                               : (total_delta_us + sequence_delta / 2u) /
                                                     sequence_delta;
              const bool interval_bad = timestamp_regressed || sequence_repeated ||
                                        interval_us < 250 || interval_us > 4000;
              if (interval_bad) {
                timestamp_check.good_streak = 0;
                ++timestamp_check.bad_streak;
                const bool severe = timestamp_regressed || sequence_repeated ||
                                    interval_us > 10000;
                if (!timestamp_check.alarm &&
                    (severe || timestamp_check.bad_streak >= 3)) {
                  timestamp_check.alarm = true;
                  const QString detail = timestamp_regressed
                                             ? QStringLiteral("regression/repeat: previous=%1 us current=%2 us")
                                                   .arg(timestamp_check.last_timestamp_us)
                                                   .arg(sample.timestamp_us)
                                         : sequence_repeated
                                             ? QStringLiteral("duplicate sample detected")
                                             : QStringLiteral("delta=%1 us over %2 samples (%3 us/sample)")
                                                   .arg(total_delta_us)
                                                   .arg(sequence_delta)
                                                   .arg(interval_us);
                  const QString context = QStringLiteral(
                      " | fsync=%1 synced=%2 flags=0x%3 gap=%4")
                                              .arg(sample.fsync_event ? 1 : 0)
                                              .arg(sample.timestamp_synced ? 1 : 0)
                                              .arg(sample.flags, 4, 16,
                                                   QLatin1Char('0'))
                                              .arg(sample.sample_gap ? 1 : 0);
                  updateImuTimestampAlarm(sensor, true, detail + context);
                }
              } else {
                timestamp_check.bad_streak = 0;
                if (timestamp_check.alarm) {
                  ++timestamp_check.good_streak;
                  if (timestamp_check.good_streak >= 1000) {
                    timestamp_check.alarm = false;
                    timestamp_check.good_streak = 0;
                    updateImuTimestampAlarm(sensor, false, QString());
                  }
                }
              }
            } else if (timestamp_valid) {
              timestamp_check.initialized = true;
            }
            if (timestamp_valid) {
              timestamp_check.initialized = true;
              timestamp_check.last_timestamp_us = sample.timestamp_us;
              timestamp_check.last_sequence = current_sequence;
              timestamp_check.last_timestamp_synced = sample.timestamp_synced;
            } else {
              timestamp_check.initialized = false;
            }

            if (now - last_imu_post[sensor] >= kImuUiPeriod) {
              last_imu_post[sensor] = now;
              const double sample_rate = rate_tracker.rate(now);
              queueImuUiUpdate(sample, received_count, sample_rate,
                               received_fsync_events[sensor],
                               last_fsync_sample_us[sensor],
                               last_fsync_delay_valid[sensor]);
            }
          });
      prism_runtime::LidarStream lidar_stream(
          client_, [this, requested_lidar_model, &pending_lidar_preview,
                   &received_lidar_points, &last_lidar_preview_post](
                       const prism::LidarPointBatch& batch) {
            if (batch.model != requested_lidar_model) {
              throw std::runtime_error(
                  "received LiDAR points for a model other than the explicit selection");
            }
            dataset_recorder_.appendLidar(batch);
            received_lidar_points += batch.points.size();
            const auto now = std::chrono::steady_clock::now();
            if (!lidar_ui_enabled_.load(std::memory_order_acquire)) {
              pending_lidar_preview.clear();
              last_lidar_preview_post = now;
              return;
            }
            pending_lidar_preview.insert(pending_lidar_preview.end(),
                                         batch.points.begin(),
                                         batch.points.end());
            if (now - last_lidar_preview_post <
                std::chrono::milliseconds(50)) {
              return;
            }
            last_lidar_preview_post = now;
            queueLidarPreview(std::move(pending_lidar_preview), batch.model,
                              received_lidar_points, batch.batch_id,
                              batch.timestamp_synced);
            pending_lidar_preview.clear();
            pending_lidar_preview.reserve(8192u);
          },
          [this, requested_lidar_model](
              const prism::LidarImuSample& sample) {
            if (sample.model != requested_lidar_model) {
              throw std::runtime_error(
                  "received LiDAR IMU for a model other than the explicit selection");
            }
            dataset_recorder_.appendLidarImu(sample);
          });
      try {
        imu_stream.start();
      } catch (...) {
        if (video_started) {
          try {
            aggregate_stream_stop_attempted = true;
            client_.stopVideo();
            video_started = false;
          } catch (const std::exception& stop_error) {
            appendLog(QStringLiteral(
                          "Capture rollback failed after IMU start error: %1")
                          .arg(stop_error.what()));
          }
        }
        throw;
      }
      appendLog(QStringLiteral("IMU started through agent SDK ImuStream"));
      if (requested_lidar_model != prism::LidarModel::None) {
        try {
          lidar_stream.start(requested_lidar_model);
          updateLidarStatus(client_.lidarStatus());
          appendLog(QStringLiteral("LiDAR started model=%1")
                        .arg(requested_lidar_model == prism::LidarModel::Mid360
                                 ? QStringLiteral("Mid-360")
                                 : QStringLiteral("Mid-360S")));
        } catch (...) {
          try {
            aggregate_stream_stop_attempted = true;
            imu_stream.stop();
            video_started = false;
          } catch (const std::exception& stop_error) {
            appendLog(QStringLiteral(
                          "Capture rollback failed after LiDAR start error: %1")
                          .arg(stop_error.what()));
          }
          throw;
        }
      }
      updateStatus(uiText("Running", "运行中"));

      const auto capture_started_at = std::chrono::steady_clock::now();
      last_completed_camera_frame_set_at = capture_started_at;
      last_usb_frame_at = capture_started_at;
      last_video_chunk_at = capture_started_at;
      auto next_device_info_query =
          capture_started_at + std::chrono::milliseconds(500);
      appendLog(QStringLiteral(
                    "Camera frame-set progress watchdog armed: timeout=%1 ms; "
                    "only complete four-camera frame sets count as progress")
                    .arg(std::chrono::duration_cast<std::chrono::milliseconds>(
                             kCameraFrameSetProgressTimeout)
                             .count()));
      auto throwIfCameraFrameSetProgressStalled =
          [&](std::chrono::steady_clock::time_point now) {
            const auto camera_progress_age =
                now - last_completed_camera_frame_set_at;
            if (camera_progress_age < kCameraFrameSetProgressTimeout) return;

            camera_progress_stalled = true;
            const auto stalled_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    camera_progress_age)
                    .count();
            const auto usb_idle_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - last_usb_frame_at)
                    .count();
            std::ostringstream error;
            error << "Camera frame-set progress stalled: no complete "
                     "four-camera frame set for "
                  << stalled_ms << " ms";
            if (last_completed_camera_frame_id.has_value()) {
              error << ", last complete frame-id="
                    << *last_completed_camera_frame_id;
            } else {
              error << ", no complete frame set received since stream start";
            }
            error << ", last delivered USB frame=" << usb_idle_ms << " ms ago";
            if (last_video_chunk_frame_id.has_value()) {
              const auto video_idle_ms =
                  std::chrono::duration_cast<std::chrono::milliseconds>(
                      now - last_video_chunk_at)
                      .count();
              error << ", video chunks received=" << received_video_chunks
                    << ", last video chunk frame-id="
                    << *last_video_chunk_frame_id << " (" << video_idle_ms
                    << " ms ago)"
                    << ", discarded incomplete frame sets="
                    << discarded_incomplete_camera_frame_sets;
            } else {
              error << ", no video chunks delivered to the capture loop";
            }
            if (capture_device_info.has_value()) {
              error << ", last-observed camera-streaming-mask=0x" << std::hex
                    << static_cast<unsigned int>(
                           capture_device_info->camera_streaming_mask)
                    << ", sensor-board-error-flags=0x"
                    << capture_device_info->sensor_board_error_flags << std::dec;
              if (capture_device_info_at.has_value()) {
                const auto device_info_age_ms =
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        now - *capture_device_info_at)
                        .count();
                error << " (" << device_info_age_ms << " ms old)";
              }
            }
            error << ". Stopping camera and IMU streams.";
            throw std::runtime_error(error.str());
          };
      unsigned consecutive_usb_read_errors = 0;
      std::exception_ptr capture_error;
      try {
        while (!stop_requested_) {
          /*
           * SDK commands synchronously consume the shared USB IN endpoint and
           * defer stream frames until the command response arrives. Exclude
           * that interval from the camera-progress budget: the capture worker
           * cannot assemble or acknowledge a frame set while it is inside
           * command().
           */
          const auto exposure_operation_started_at =
              std::chrono::steady_clock::now();
          throwIfCameraFrameSetProgressStalled(exposure_operation_started_at);
          const auto camera_progress_age_before_exposure =
              exposure_operation_started_at -
              last_completed_camera_frame_set_at;
          const bool exposure_operation_processed =
              processPendingCameraExposureOperation();
          auto loop_now = std::chrono::steady_clock::now();
          if (exposure_operation_processed) {
            /*
             * A live exposure transaction can fill the SDK deferred-frame
             * queue. If progress was fresh when the command began, give the
             * worker a full watchdog interval to drain it. A request made after
             * progress was already stale may compensate only for its own
             * synchronous command time; repeated requests must not conceal an
             * existing camera stall.
             */
            if (camera_progress_age_before_exposure <
                kCameraControlCommandFreshnessLimit) {
              last_completed_camera_frame_set_at = loop_now;
            } else {
              last_completed_camera_frame_set_at +=
                  loop_now - exposure_operation_started_at;
            }
            /*
             * Do not chain periodic commands immediately after exposure. Drain
             * the SDK's deferred stream frames first so the four-frame video
             * credit window can be acknowledged promptly.
             */
            next_device_info_query = loop_now + std::chrono::seconds(1);
            appendLog(QStringLiteral(
                "Runtime exposure operation processed; prioritizing deferred "
                "camera and IMU frames"));
          }

          throwIfCameraFrameSetProgressStalled(loop_now);
          const auto camera_progress_age =
              loop_now - last_completed_camera_frame_set_at;

          /*
           * Periodic status commands are useful only while camera progress is
           * fresh. Once progress is questionable, dedicate the shared receiver
           * to stream draining until a complete frame arrives or the watchdog
           * expires. Version data is static for an open session and is
           * therefore not refreshed from the capture loop.
           */
          if (loop_now >= next_device_info_query &&
              camera_progress_age < kCameraControlCommandFreshnessLimit) {
            const auto query_started_at = std::chrono::steady_clock::now();
            try {
              const auto info = client_.deviceInfo();
              capture_device_info = info;
              capture_device_info_at = std::chrono::steady_clock::now();
              updateDeviceInfo(info);
              if (!info.sensor_board_online) {
                appendLog(QStringLiteral(
                    "DeviceInfo reports sensor-board offline; stopping camera "
                    "and IMU streams"));
                updateStatus(
                    uiText("RK/sensor-board link lost; stopping capture",
                           "RK 与 sensor-board 连接中断，正在停止采集"));
                stop_requested_ = true;
                continue;
              }
              if (requested_lidar_model != prism::LidarModel::None) {
                try {
                  updateLidarStatus(client_.lidarStatus());
                } catch (const std::exception& lidar_error) {
                  appendLog(QStringLiteral("LiDAR status refresh failed: %1")
                                .arg(lidar_error.what()));
                }
              }
            } catch (const std::exception& ex) {
              appendLog(QStringLiteral("DeviceInfo refresh failed: %1")
                            .arg(ex.what()));
            }
            const auto query_finished_at = std::chrono::steady_clock::now();
            last_completed_camera_frame_set_at +=
                query_finished_at - query_started_at;
            next_device_info_query =
                query_finished_at + std::chrono::seconds(1);
            loop_now = query_finished_at;
          }

          prism::Frame frame;
          try {
            frame = client_.readFrame(1000);
          } catch (const std::exception& ex) {
            ++consecutive_usb_read_errors;
            if (consecutive_usb_read_errors == 1) {
              appendLog(
                  QStringLiteral("USB frame read failed: %1").arg(ex.what()));
            }
            if (!client_.keepaliveEnabled() ||
                consecutive_usb_read_errors >= 3) {
              throw std::runtime_error(
                  "USB transport stopped responding after " +
                  std::to_string(consecutive_usb_read_errors) +
                  " consecutive read failures: " + ex.what());
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
          }
          consecutive_usb_read_errors = 0;
          last_usb_frame_at = std::chrono::steady_clock::now();

          if (imu_stream.handleFrame(frame)) {
            continue;
          }
          if (lidar_stream.handleFrame(frame)) {
            continue;
          }

          if (frame.type == prism::FrameType::Heartbeat) {
            const auto heartbeat = prism_runtime::parseHeartbeat(frame);
            updateHeartbeat(heartbeat);
          } else if (frame.type == prism::FrameType::VideoChunk) {
            const auto chunk = prism_runtime::parseVideoChunkView(frame);
            last_video_chunk_at = std::chrono::steady_clock::now();
            last_video_chunk_frame_id = chunk.frame_id;
            ++received_video_chunks;
            auto result = camera_assembler.ingest(chunk);
            for (uint32_t discarded : result.discarded_incomplete_frame_ids) {
              ++discarded_incomplete_camera_frame_sets;
              appendLog(
                  QStringLiteral(
                      "Discarding and acknowledging incomplete or corrupt "
                      "four-camera frame set %1")
                      .arg(discarded));
              /*
               * A transmitted frame set consumes one server flow-control
               * credit even when one camera image is corrupt or incomplete.
               * The assembler emits every retired ID exactly once.
               */
              client_.sendVideoAck(discarded);
            }
            if (!result.completed.has_value()) continue;
            handleCompletedCameraFrame(std::move(*result.completed));
          } else if (frame.type == prism::FrameType::VideoMeta) {
            const auto meta = prism_runtime::parseVideoMeta(frame);
            std::optional<prism_viewer::transfer::CameraFrameSet> completed =
                camera_assembler.addMetadata(meta);
            if (completed.has_value()) {
              handleCompletedCameraFrame(std::move(*completed));
            }
            const auto metadata_now = std::chrono::steady_clock::now();
            if (camera_preview_enabled_.load(std::memory_order_acquire) &&
                metadata_now >= next_metadata_ui_update) {
              updateMeta(meta);
              next_metadata_ui_update = metadata_now + kMetadataUiPeriod;
            }
          }
        }
      } catch (...) {
        capture_error = std::current_exception();
      }

      std::exception_ptr lidar_stop_error;
      try {
        lidar_stream.stop();
      } catch (...) {
        lidar_stop_error = std::current_exception();
      }
      std::exception_ptr stop_error;
      aggregate_stream_stop_attempted = true;
      try {
        imu_stream.stop();
        // IMU_STOP is the aggregate Camera + IMU stop transaction.
        video_started = false;
      } catch (...) {
        stop_error = std::current_exception();
      }
      if (capture_error) {
        if (lidar_stop_error) {
          try {
            std::rethrow_exception(lidar_stop_error);
          } catch (const std::exception& ex) {
            appendLog(QStringLiteral(
                          "Capture error cleanup could not confirm LiDAR stop: %1")
                          .arg(ex.what()));
          }
        }
        if (stop_error) {
          try {
            std::rethrow_exception(stop_error);
          } catch (const std::exception& ex) {
            appendLog(
                QStringLiteral("Capture error cleanup could not confirm "
                               "aggregate stream stop: %1")
                    .arg(ex.what()));
          }
        }
        std::rethrow_exception(capture_error);
      }
      if (lidar_stop_error) std::rethrow_exception(lidar_stop_error);
      if (stop_error) std::rethrow_exception(stop_error);
      appendLog(QStringLiteral("Streams stopped"));
      updateStatus(uiText("Stopped", "已停止"));
    } catch (const std::exception& ex) {
      appendLog(QStringLiteral("Error: %1").arg(ex.what()));
      updateStatus(uiText("Error: %1", "错误：%1").arg(ex.what()));
      /*
       * Attempt the aggregate stop at most once. This still covers a remote
       * stream that started before its start acknowledgement was lost. Any
       * capture transaction error then closes the transport because private
       * SDK deferred frames cannot be safely reused by a fresh assembler.
       */
      if (aggregate_stream_start_attempted &&
          !aggregate_stream_stop_attempted && client_.isOpen()) {
        aggregate_stream_stop_attempted = true;
        try {
          client_.stopVideo();
          appendLog(QStringLiteral(
              "Capture error rollback stopped camera and IMU streams"));
        } catch (const std::exception& stop_error) {
          appendLog(QStringLiteral(
                        "Capture error rollback could not confirm stream stop: %1")
                        .arg(stop_error.what()));
        }
      }
      if (aggregate_stream_start_attempted && client_.isOpen()) {
        client_.closeDevice();
        appendLog(camera_progress_stalled
                      ? QStringLiteral(
                            "USB session closed after camera frame-set stall "
                            "to discard queued stream frames; reopen the device "
                            "before retrying")
                      : QStringLiteral(
                            "USB session closed after capture transaction "
                            "error to discard queued stream frames; reopen the "
                            "device before retrying"));
        post([this]() {
          latest_device_info_valid_ = false;
          latest_device_versions_valid_ = false;
          latest_rk_heartbeat_time_us_ = 0;
          device_info_panel_->setDeviceOpen(false);
          camera_encoding_panel_->setDeviceOpen(false);
          camera_exposure_panel_->setDeviceOpen(false);
          wifi_hotspot_panel_->setDeviceOpen(false);
          time_sync_label_->setText(
              uiText("Time sync: device closed", "时间同步：设备已关闭"));
          host_time_sync_label_->setText(uiText(
              "Host/device clock: device closed", "主机/设备时钟：设备已关闭"));
        });
      }
      appendLog(client_.isOpen()
                    ? QStringLiteral(
                          "USB device remains open after capture error")
                    : QStringLiteral(
                          "USB transport is no longer available after capture error"));
    }

    cancelPendingCameraExposureOperation(
        uiText("Capture stopped before the exposure request was processed",
               "采集已停止，曝光请求尚未执行"));
    worker_running_ = false;
    post([this]() {
      if (dataset_recorder_.isActive()) stopImuRecording();
      refreshControls();
    });
  }

  QComboBox* device_selector_ = nullptr;
  QPushButton* refresh_devices_button_ = nullptr;
  QPushButton* open_device_button_ = nullptr;
  QPushButton* close_device_button_ = nullptr;
  QPushButton* start_button_ = nullptr;
  QPushButton* stop_button_ = nullptr;
  QPushButton* host_time_sync_button_ = nullptr;
  QPushButton* system_upgrade_button_ = nullptr;
  QPushButton* log_button_ = nullptr;
  QComboBox* language_selector_ = nullptr;
  QLabel* status_label_ = nullptr;
  QLabel* time_sync_label_ = nullptr;
  QLabel* host_time_sync_label_ = nullptr;
  QTabWidget* tabs_ = nullptr;
  DeviceInfoPanel* device_info_panel_ = nullptr;
  QWidget* camera_page_ = nullptr;
  CameraEncodingPanel* camera_encoding_panel_ = nullptr;
  CameraExposurePanel* camera_exposure_panel_ = nullptr;
  QWidget* imu_page_ = nullptr;
  QWidget* lidar_page_ = nullptr;
  QCheckBox* lidar_enabled_checkbox_ = nullptr;
  QComboBox* lidar_model_selector_ = nullptr;
  QSpinBox* lidar_point_size_spin_ = nullptr;
  QLabel* lidar_status_label_ = nullptr;
  QCheckBox* lidar_network_enabled_checkbox_ = nullptr;
  QLineEdit* lidar_network_host_ip_ = nullptr;
  QLineEdit* lidar_network_netmask_ = nullptr;
  QLineEdit* lidar_network_target_ip_ = nullptr;
  QPushButton* lidar_network_refresh_button_ = nullptr;
  QPushButton* lidar_network_apply_button_ = nullptr;
  QPushButton* lidar_network_probe_button_ = nullptr;
  QLabel* lidar_network_status_label_ = nullptr;
  prism_viewer::LidarPointCloudWidget* lidar_point_cloud_widget_ = nullptr;
  WifiHotspotPanel* wifi_hotspot_panel_ = nullptr;
  QWidget* dataset_page_ = nullptr;
  std::array<ImageViewLabel*, 4> image_labels_{};
  std::array<QLabel*, 4> frame_labels_{};
  CameraZoomDialog* live_camera_zoom_dialog_ = nullptr;
  std::array<QImage, 4> latest_camera_images_{};
  uint32_t latest_camera_frame_id_ = 0;
  QPushButton* dataset_open_button_ = nullptr;
  QPushButton* dataset_validate_button_ = nullptr;
  QPushButton* dataset_imu_alignment_button_ = nullptr;
  QPushButton* dataset_export_rosbag_button_ = nullptr;
  QLabel* dataset_path_label_ = nullptr;
  QLabel* dataset_summary_label_ = nullptr;
  QLabel* dataset_frame_label_ = nullptr;
  QSlider* dataset_frame_slider_ = nullptr;
  std::array<ImageViewLabel*, 4> dataset_image_labels_{};
  QPlainTextEdit* dataset_details_ = nullptr;
  CameraZoomDialog* dataset_camera_zoom_dialog_ = nullptr;
  std::array<std::vector<DatasetImageEntry>, 4> dataset_camera_entries_{};
  std::array<QImage, 4> dataset_images_{};
  size_t dataset_frame_count_ = 0;
  int dataset_current_frame_ = 0;
  QString loaded_dataset_root_;
  bool dataset_has_imu_alignment_inputs_ = false;
  bool rosbag_export_running_ = false;
  QPlainTextEdit* meta_text_ = nullptr;
  QPlainTextEdit* log_text_ = nullptr;
  QDialog* log_dialog_ = nullptr;
  QCheckBox* log_auto_scroll_ = nullptr;
  QTableWidget* imu_table_ = nullptr;
  QButtonGroup* imu_selector_group_ = nullptr;
  QPushButton* imu0_selector_ = nullptr;
  QPushButton* imu1_selector_ = nullptr;
  QComboBox* imu_acceleration_unit_selector_ = nullptr;
  QComboBox* imu_angular_velocity_unit_selector_ = nullptr;
  QComboBox* imu_temperature_unit_selector_ = nullptr;
  QPushButton* imu_record_start_button_ = nullptr;
  QPushButton* imu_record_stop_button_ = nullptr;
  QLabel* imu_alarm_label_ = nullptr;
  QLabel* imu_record_status_label_ = nullptr;
  ImuPlotWidget* imu_plot_ = nullptr;
  QTimer* imu_ui_timer_ = nullptr;
  prism_viewer::communication::DeviceSession device_session_;
  prism_runtime::Client& client_ = device_session_.client();
  const std::vector<prism::DeviceInfo>& devices_ = device_session_.devices();
  DatasetRecorder dataset_recorder_;
  QString recorded_dataset_root_;

  prism_viewer::control::OperationController operation_controller_;
  std::array<std::thread, 2> camera_preview_workers_;
  std::atomic<bool> stop_requested_{false};
  std::atomic<bool> worker_running_{false};
  std::atomic<bool> upgrade_running_{false};
  std::atomic<bool> time_sync_running_{false};
  std::atomic<bool> wifi_operation_running_{false};
  std::atomic<bool> camera_exposure_operation_running_{false};
  std::atomic<bool> camera_encoding_operation_running_{false};
  std::atomic<bool> lidar_network_operation_running_{false};
  std::atomic<bool> camera_preview_enabled_{false};
  std::atomic<bool> imu_ui_enabled_{false};
  std::atomic<bool> lidar_ui_enabled_{false};
  std::atomic<int> requested_lidar_model_{
      static_cast<int>(prism::LidarModel::None)};
  std::atomic<bool> live_camera_zoom_visible_{false};
  std::atomic<int> live_camera_zoom_camera_{0};
  std::atomic<uint64_t> camera_preview_generation_{0};
  std::atomic<size_t> camera_preview_ui_posts_pending_{0};
  std::atomic<int> camera_preview_width_{kCameraPreviewWidth};
  std::atomic<int> camera_preview_height_{kCameraPreviewHeight};
  std::mutex camera_preview_mutex_;
  std::mutex imu_ui_mutex_;
  std::mutex camera_exposure_request_mutex_;
  std::optional<CameraExposureOperationRequest>
      pending_camera_exposure_request_;
  std::condition_variable camera_preview_wakeup_;
  std::deque<CameraPreviewJob> camera_preview_jobs_;
  std::map<uint64_t, DecodedCameraPreviewJob> camera_preview_completed_;
  std::array<ImuUiSnapshot, 2> pending_imu_ui_{};
  std::array<bool, 2> pending_imu_ui_dirty_{};
  std::array<std::deque<PendingImuPlotSample>, 2>
      pending_imu_plot_samples_{};
  uint64_t camera_preview_next_sequence_ = 1;
  bool camera_preview_stop_ = false;
  std::chrono::steady_clock::time_point start_time_ = std::chrono::steady_clock::now();
  std::array<uint64_t, 4> camera_frames_{};
  uint64_t camera_frame_sets_ = 0;
  std::array<uint64_t, 2> imu_samples_{};
  std::array<uint64_t, 2> imu_fsync_events_{};
  std::array<bool, 2> imu_timestamp_alarm_{};
  std::array<QString, 2> imu_timestamp_alarm_detail_{};
  AccelerationUnit acceleration_unit_ =
      prism_viewer::imu_units::kDefaultAccelerationUnit;
  AngularVelocityUnit angular_velocity_unit_ =
      prism_viewer::imu_units::kDefaultAngularVelocityUnit;
  TemperatureUnit temperature_unit_ =
      prism_viewer::imu_units::kDefaultTemperatureUnit;
  prism::DeviceInfo latest_device_info_;
  prism::DeviceVersions latest_device_versions_;
  uint64_t latest_rk_heartbeat_time_us_ = 0;
  bool latest_device_info_valid_ = false;
  bool latest_device_versions_valid_ = false;
};

}  // namespace

namespace prism_viewer {

int runViewerApplication(int argc, char** argv) {
#if defined(Q_OS_LINUX)
  // Prism Viewer does not implement XSMP session restore. A stale
  // SESSION_MANAGER inherited from sudo, SSH, or a detached terminal makes Qt
  // attempt an ICE connection and print an authentication error before the
  // window opens. Ignore only that optional session-manager endpoint; the
  // display and X11/Wayland authentication environment remains untouched.
  qunsetenv("SESSION_MANAGER");
#endif
  QApplication app(argc, argv);
  ui::applyLightApplicationTheme(app);
  app.setApplicationName(QStringLiteral("Prism Viewer"));
  app.setOrganizationName(QStringLiteral("Prism"));
  app.setWindowIcon(QIcon(QStringLiteral(":/branding/prism-mark.png")));
  const QStringList command_line = QCoreApplication::arguments();
  int dataset_self_test =
      command_line.indexOf(QStringLiteral("--dataset-recorder-self-test"));
  const int imu_only_self_test = command_line.indexOf(
      QStringLiteral("--imu-only-recorder-self-test"));
  const bool test_imu_only = dataset_self_test < 0 && imu_only_self_test >= 0;
  if (test_imu_only) dataset_self_test = imu_only_self_test;
  if (dataset_self_test >= 0 &&
      dataset_self_test + 1 < command_line.size()) {
    const auto test_root =
        toFilesystemPath(command_line[dataset_self_test + 1]);
    if (test_imu_only) {
      std::error_code filesystem_error;
      std::filesystem::create_directories(test_root, filesystem_error);
      if (filesystem_error) {
        std::cerr << "cannot create IMU-only self-test directory\n";
        return 9;
      }
      const std::array<std::filesystem::path, 8> stale_full_outputs = {
          test_root / "cam0.tum", test_root / "cam1.tum",
          test_root / "cam2.tum", test_root / "cam3.tum",
          test_root / "lidar.tum", test_root / "lidar_imu.tum",
          test_root / "camera-data-0000.bin",
          test_root / "lidar-data-0000.bin"};
      for (const auto& path : stale_full_outputs) {
        std::ofstream stale_file(path, std::ios::out | std::ios::trunc);
        stale_file << "stale full-dataset output\n";
        if (!stale_file.good()) {
          std::cerr << "cannot seed IMU-only overwrite self-test\n";
          return 9;
        }
      }
    }
    DatasetRecorder recorder;
    std::string error;
    if (!recorder.start(
            test_root, true,
            test_imu_only ? DatasetRecordingMode::ImuOnly
                          : DatasetRecordingMode::Full,
            true, &error)) {
      std::cerr << "dataset recorder start failed: " << error << "\n";
      return 10;
    }
    bool in_progress_manifest_ok = false;
    {
      std::ifstream in_progress_manifest(test_root / "dataset.info");
      for (std::string line;
           std::getline(in_progress_manifest, line);) {
        in_progress_manifest_ok =
            in_progress_manifest_ok || line == "complete=0";
      }
    }
    prism::ImuSample imu0;
    imu0.sensor_id = 0;
    imu0.timestamp_us = 1780000000000000ULL;
    imu0.timestamp_synced = true;
    prism::ImuSample imu1 = imu0;
    imu1.sensor_id = 1;
    imu1.timestamp_us += 100;
    prism::ImuSample unsynced_imu0 = imu0;
    unsynced_imu0.timestamp_synced = false;
    prism::ImuSample unsynced_imu1 = imu1;
    unsynced_imu1.timestamp_synced = false;
    recorder.appendImu(unsynced_imu0);
    recorder.appendImu(unsynced_imu1);
    recorder.appendImu(imu0);
    recorder.appendImu(imu1);
    std::array<std::vector<uint8_t>, 4> jpeg;
    for (size_t camera = 0; camera < jpeg.size(); ++camera) {
      QImage test_image(64, 48, QImage::Format_RGB888);
      test_image.fill(QColor::fromHsv(static_cast<int>(camera) * 75, 220, 230));
      QByteArray encoded;
      QBuffer buffer(&encoded);
      buffer.open(QIODevice::WriteOnly);
      test_image.save(&buffer, "JPG", 90);
      jpeg[camera].assign(encoded.begin(), encoded.end());
    }
    prism::VideoMeta metadata;
    metadata.valid = true;
    metadata.host_frame_id = 7;
    metadata.trigger_time_ns = 1780000000000500000ULL;
    metadata.exposure_us = {200u, 250u, 500u, 1000u};
    prism::VideoMeta unsynced_metadata = metadata;
    unsynced_metadata.host_frame_id = 6;
    unsynced_metadata.trigger_time_ns = 0;
    recorder.appendFrameSet(6, 0, unsynced_metadata, jpeg);
    recorder.appendFrameSet(
        7, 1780000000000500ULL, metadata, jpeg);
    prism::LidarPointBatch lidar;
    lidar.model = prism::LidarModel::Mid360S;
    lidar.device_type = 35u;
    lidar.time_type = 1u;
    lidar.batch_id = 9u;
    lidar.timestamp_raw = 1780000000000700000ULL;
    lidar.timestamp_utc_us = 1780000000000700ULL;
    lidar.time_interval_100ns = 100u;
    lidar.flags = 0x03u;
    lidar.timestamp_synced = true;
    lidar.tai_offset_applied = true;
    lidar.points.push_back(
        prism::LidarPoint{1000, -2000, 3000, 77u, 4u});
    lidar.points.push_back(
        prism::LidarPoint{-4000, 5000, 6000, 88u, 5u});
    prism::LidarPointBatch unsynced_lidar = lidar;
    unsynced_lidar.flags = 0u;
    unsynced_lidar.timestamp_synced = false;
    unsynced_lidar.tai_offset_applied = false;
    unsynced_lidar.timestamp_utc_us = 0;
    recorder.appendLidar(unsynced_lidar);
    recorder.appendLidar(lidar);
    prism::LidarImuSample lidar_imu;
    lidar_imu.model = prism::LidarModel::Mid360S;
    lidar_imu.device_type = 35u;
    lidar_imu.time_type = 1u;
    lidar_imu.sample_id = 10u;
    lidar_imu.timestamp_raw_ns = 1780000000000800000ULL;
    lidar_imu.timestamp_utc_us = 1780000000000800ULL;
    lidar_imu.timestamp_synced = true;
    lidar_imu.tai_offset_applied = true;
    lidar_imu.accel_m_s2 = {0.1f, -0.2f, 9.7f};
    lidar_imu.gyro_rad_s = {0.01f, -0.02f, 0.03f};
    prism::LidarImuSample unsynced_lidar_imu = lidar_imu;
    unsynced_lidar_imu.timestamp_synced = false;
    unsynced_lidar_imu.tai_offset_applied = false;
    unsynced_lidar_imu.timestamp_utc_us = 0;
    recorder.appendLidarImu(unsynced_lidar_imu);
    recorder.appendLidarImu(lidar_imu);
    const DatasetRecordingSummary summary = recorder.stop();
    std::array<std::vector<DatasetImageEntry>, 4> loaded_images;
    bool browser_load_ok = true;
    if (!test_imu_only) {
      for (size_t camera = 0; camera < loaded_images.size(); ++camera) {
        browser_load_ok =
            browser_load_ok &&
            loadDatasetImageIndex(
                test_root, camera, &loaded_images[camera], &error) &&
            loaded_images[camera].size() == 1 &&
            loaded_images[camera][0].timestamp_us == 1780000000000500ULL &&
            loaded_images[camera][0].exposure_us ==
                metadata.exposure_us[camera] &&
            !loadDatasetImage(loaded_images[camera][0]).isNull();
      }
    }
    const TumFileSummary loaded_imu0 =
        summarizeTumFile(test_root / "imu0.tum");
    const TumFileSummary loaded_imu1 =
        summarizeTumFile(test_root / "imu1.tum");
    const TumFileSummary loaded_lidar =
        summarizeTumFile(test_root / "lidar.tum");
    const TumFileSummary loaded_lidar_imu =
        summarizeTumFile(test_root / "lidar_imu.tum");
    const auto firstDataLine = [](const std::filesystem::path& path) {
      std::ifstream input(path);
      for (std::string line; std::getline(input, line);) {
        if (!line.empty() && line[0] != '#') return line;
      }
      return std::string();
    };
    bool lidar_v6_source_ok = test_imu_only;
    if (!test_imu_only) {
      std::istringstream parser(firstDataLine(test_root / "lidar.tum"));
      std::string timestamp;
      std::string container;
      uint64_t offset = 0;
      uint64_t byte_size = 0;
      uint64_t point_count = 0;
      uint32_t model = 0;
      uint32_t device_type = 0;
      uint32_t time_type = 0;
      uint32_t batch_id = 0;
      uint64_t timestamp_raw = 0;
      uint32_t time_interval_100ns = 0;
      uint32_t timestamp_synced = 0;
      uint32_t tai_offset_applied = 0;
      std::string trailing;
      lidar_v6_source_ok =
          static_cast<bool>(
              parser >> timestamp >> container >> offset >> byte_size >>
              point_count >> model >> device_type >> time_type >> batch_id >>
              timestamp_raw >> time_interval_100ns >> timestamp_synced >>
              tai_offset_applied) &&
          !(parser >> trailing) && timestamp == "1780000000.000700" &&
          time_interval_100ns == 100u && timestamp_synced == 1u &&
          tai_offset_applied == 1u;
    }
    std::istringstream lidar_imu_parser(
        firstDataLine(test_root / "lidar_imu.tum"));
    std::array<std::string, 7> lidar_imu_values;
    uint32_t lidar_imu_model = 0;
    uint32_t lidar_imu_device_type = 0;
    uint32_t lidar_imu_time_type = 0;
    uint32_t lidar_imu_sample_id = 0;
    uint64_t lidar_imu_timestamp_raw = 0;
    uint32_t lidar_imu_timestamp_synced = 0;
    uint32_t lidar_imu_tai_offset_applied = 0;
    std::string lidar_imu_trailing;
    bool lidar_imu_v6_source_ok = true;
    for (auto& value : lidar_imu_values) {
      lidar_imu_v6_source_ok =
          lidar_imu_v6_source_ok && static_cast<bool>(lidar_imu_parser >> value);
    }
    lidar_imu_v6_source_ok =
        lidar_imu_v6_source_ok &&
        static_cast<bool>(lidar_imu_parser >> lidar_imu_model >>
                          lidar_imu_device_type >> lidar_imu_time_type >>
                          lidar_imu_sample_id >> lidar_imu_timestamp_raw >>
                          lidar_imu_timestamp_synced >>
                          lidar_imu_tai_offset_applied) &&
        !(lidar_imu_parser >> lidar_imu_trailing) &&
        lidar_imu_values[0] == "1780000000.000800" &&
        lidar_imu_timestamp_synced == 1u &&
        lidar_imu_tai_offset_applied == 1u;
    browser_load_ok =
        browser_load_ok && loaded_imu0.rows == 1 &&
        loaded_imu0.first_timestamp_us == 1780000000000000ULL &&
        loaded_imu1.rows == 1 &&
        loaded_imu1.first_timestamp_us == 1780000000000100ULL &&
        loaded_lidar_imu.rows == 1 &&
        loaded_lidar_imu.first_timestamp_us == 1780000000000800ULL;
    bool auxiliary_streams_ok = true;
    if (test_imu_only) {
      auxiliary_streams_ok = loaded_lidar.rows == 0 &&
                             !std::filesystem::exists(test_root / "lidar.tum") &&
                             summary.lidar_batch_count == 0 &&
                             summary.lidar_point_count == 0 &&
                             summary.lidar_imu_sample_count == 1 &&
                             std::all_of(
                                 summary.image_count.begin(),
                                 summary.image_count.end(),
                                 [](uint64_t count) { return count == 0; });
      for (size_t camera = 0; camera < loaded_images.size(); ++camera) {
        auxiliary_streams_ok =
            auxiliary_streams_ok &&
            !std::filesystem::exists(
                test_root / ("cam" + std::to_string(camera) + ".tum"));
      }
      for (const auto& entry : std::filesystem::directory_iterator(test_root)) {
        auxiliary_streams_ok =
            auxiliary_streams_ok && entry.path().extension() != ".bin";
      }
    } else {
      auxiliary_streams_ok =
          loaded_lidar.rows == 1 &&
          loaded_lidar.first_timestamp_us == 1780000000000700ULL &&
          summary.lidar_batch_count == 1 &&
          summary.lidar_point_count == 2 &&
          summary.lidar_imu_sample_count == 1 &&
          std::all_of(summary.image_count.begin(), summary.image_count.end(),
                      [](uint64_t count) { return count == 1; });
    }
    bool manifest_mode_ok = false;
    bool manifest_complete_ok = false;
    bool manifest_time_domain_ok = false;
    bool manifest_epoch_ok = false;
    bool manifest_alignment_ok = false;
    std::array<bool, 6> manifest_unsynced_drop_fields{};
    const std::array<std::string, 6> expected_unsynced_drop_fields = {
        "unsynced_imu0_samples_dropped=1",
        "unsynced_imu1_samples_dropped=1",
        std::string("unsynced_camera_frame_sets_dropped=") +
            (test_imu_only ? "0" : "1"),
        std::string("unsynced_lidar_batches_dropped=") +
            (test_imu_only ? "0" : "1"),
        std::string("unsynced_lidar_points_dropped=") +
            (test_imu_only ? "0" : "2"),
        "unsynced_lidar_imu_samples_dropped=1"};
    std::ifstream manifest(test_root / "dataset.info");
    const std::string expected_mode =
        test_imu_only ? "recording_mode=imu-only" : "recording_mode=full";
    for (std::string line; std::getline(manifest, line);) {
      manifest_mode_ok = manifest_mode_ok || line == expected_mode;
      manifest_complete_ok = manifest_complete_ok || line == "complete=1";
      manifest_time_domain_ok = manifest_time_domain_ok ||
                                line == "time_domain=rk-clock-realtime";
      manifest_epoch_ok =
          manifest_epoch_ok || line == "timestamp_epoch=unix";
      manifest_alignment_ok = manifest_alignment_ok ||
                              line == "alignment=common-device-time-domain";
      for (size_t field = 0; field < expected_unsynced_drop_fields.size();
           ++field) {
        manifest_unsynced_drop_fields[field] =
            manifest_unsynced_drop_fields[field] ||
            line == expected_unsynced_drop_fields[field];
      }
    }
    const bool manifest_unsynced_drops_ok = std::all_of(
        manifest_unsynced_drop_fields.begin(),
        manifest_unsynced_drop_fields.end(), [](bool found) { return found; });
    const bool mode_ok =
        summary.mode == (test_imu_only ? DatasetRecordingMode::ImuOnly
                                       : DatasetRecordingMode::Full);
    const DatasetValidationResult recorded_validation =
        validatePrismDataset(test_root);
    const bool recorded_validation_ok =
        recorded_validation.valid && recorded_validation.errorCount() == 0u;

    // Exercise the complementary file set as well. A capture started without
    // LiDAR must not leave an empty point index, LiDAR IMU stream, or manifest
    // claim behind, even if stale/unexpected LiDAR callbacks are presented to
    // the recorder during the test.
    const std::filesystem::path no_lidar_root = test_root / "without-lidar";
    DatasetRecorder no_lidar_recorder;
    std::string no_lidar_error;
    bool no_lidar_ok = no_lidar_recorder.start(
        no_lidar_root, true,
        test_imu_only ? DatasetRecordingMode::ImuOnly
                      : DatasetRecordingMode::Full,
        false, &no_lidar_error);
    DatasetRecordingSummary no_lidar_summary;
    if (no_lidar_ok) {
      no_lidar_recorder.appendImu(imu0);
      no_lidar_recorder.appendImu(imu1);
      no_lidar_recorder.appendFrameSet(
          7, 1780000000000500ULL, metadata, jpeg);
      no_lidar_recorder.appendLidar(lidar);
      no_lidar_recorder.appendLidarImu(lidar_imu);
      no_lidar_summary = no_lidar_recorder.stop();
      no_lidar_ok =
          no_lidar_summary.success &&
          no_lidar_summary.sample_count[0] == 1 &&
          no_lidar_summary.sample_count[1] == 1 &&
          no_lidar_summary.lidar_batch_count == 0 &&
          no_lidar_summary.lidar_point_count == 0 &&
          no_lidar_summary.lidar_imu_sample_count == 0 &&
          !std::filesystem::exists(no_lidar_root / "lidar.tum") &&
          !std::filesystem::exists(no_lidar_root / "lidar_imu.tum") &&
          !std::filesystem::exists(no_lidar_root / "lidar-data-0000.bin");
      for (size_t camera = 0; camera < loaded_images.size(); ++camera) {
        const bool camera_index_exists = std::filesystem::is_regular_file(
            no_lidar_root / ("cam" + std::to_string(camera) + ".tum"));
        no_lidar_ok =
            no_lidar_ok &&
            camera_index_exists == !test_imu_only &&
            no_lidar_summary.image_count[camera] ==
                (test_imu_only ? 0u : 1u);
      }
      bool manifest_lidar_none = false;
      bool manifest_lidar_imu_none = false;
      std::ifstream no_lidar_manifest(no_lidar_root / "dataset.info");
      for (std::string line; std::getline(no_lidar_manifest, line);) {
        manifest_lidar_none =
            manifest_lidar_none || line == "lidar_storage=none";
        manifest_lidar_imu_none =
            manifest_lidar_imu_none || line == "lidar_imu_storage=none";
      }
      no_lidar_ok = no_lidar_ok && manifest_lidar_none &&
                    manifest_lidar_imu_none;
      no_lidar_ok =
          no_lidar_ok && validatePrismDataset(no_lidar_root).valid;
    }
    const bool expected_full_unsynced_drops =
        test_imu_only ||
        (summary.unsynced_camera_frame_sets_dropped == 1 &&
         summary.unsynced_lidar_batches_dropped == 1 &&
         summary.unsynced_lidar_points_dropped == 2);
    const bool strict_time_drop_counts_ok =
        summary.unsynced_imu_samples_dropped[0] == 1 &&
        summary.unsynced_imu_samples_dropped[1] == 1 &&
        summary.unsynced_lidar_imu_samples_dropped == 1 &&
        expected_full_unsynced_drops;

    const bool writer_queue_order_ok =
        shouldTakeFrameJob(true, false, 300, 0) &&
        !shouldTakeFrameJob(false, true, 0, 100) &&
        shouldTakeFrameJob(true, true, 100, 200) &&
        shouldTakeFrameJob(true, true, 100, 100) &&
        !shouldTakeFrameJob(true, true, 300, 200);

    // A recording that receives only unsynchronized samples is explicitly
    // unusable; it must not report success merely because empty files were
    // flushed without an I/O error.
    const std::filesystem::path unsynced_only_root =
        test_root / "unsynced-only";
    DatasetRecorder unsynced_only_recorder;
    std::string unsynced_only_error;
    bool unsynced_only_fails = unsynced_only_recorder.start(
        unsynced_only_root, true, DatasetRecordingMode::ImuOnly, false,
        &unsynced_only_error);
    if (unsynced_only_fails) {
      unsynced_only_recorder.appendImu(unsynced_imu0);
      unsynced_only_recorder.appendImu(unsynced_imu1);
      const DatasetRecordingSummary unsynced_only_summary =
          unsynced_only_recorder.stop();
      bool incomplete_manifest_ok = false;
      std::ifstream incomplete_manifest(unsynced_only_root / "dataset.info");
      for (std::string line; std::getline(incomplete_manifest, line);) {
        incomplete_manifest_ok =
            incomplete_manifest_ok || line == "complete=0";
      }
      unsynced_only_fails =
          !unsynced_only_summary.success &&
          unsynced_only_summary.sample_count[0] == 0 &&
          unsynced_only_summary.sample_count[1] == 0 &&
          unsynced_only_summary.unsynced_imu_samples_dropped[0] == 1 &&
          unsynced_only_summary.unsynced_imu_samples_dropped[1] == 1 &&
          unsynced_only_summary.error.find("contains no synchronized") !=
              std::string::npos &&
          incomplete_manifest_ok;
    }
    const bool success = summary.success && mode_ok && manifest_mode_ok &&
                         manifest_complete_ok &&
                         manifest_time_domain_ok && manifest_epoch_ok &&
                         manifest_alignment_ok &&
                         manifest_unsynced_drops_ok &&
                         strict_time_drop_counts_ok && lidar_v6_source_ok &&
                         lidar_imu_v6_source_ok && writer_queue_order_ok &&
                         unsynced_only_fails && in_progress_manifest_ok &&
                         summary.sample_count[0] == 1 &&
                         summary.sample_count[1] == 1 && browser_load_ok &&
                         auxiliary_streams_ok && no_lidar_ok &&
                         recorded_validation_ok;
    std::cout << (test_imu_only ? "imu_only_recorder_self_test="
                                : "dataset_recorder_self_test=")
              << (success ? "PASS" : "FAIL")
              << " imu=" << summary.sample_count[0] << "/"
              << summary.sample_count[1] << " images="
              << summary.image_count[0] << "/" << summary.image_count[1]
              << "/" << summary.image_count[2] << "/"
              << summary.image_count[3] << " lidar="
              << summary.lidar_batch_count << "/"
              << summary.lidar_point_count << " lidar_imu="
              << summary.lidar_imu_sample_count
              << " manifest_mode=" << (manifest_mode_ok ? "PASS" : "FAIL")
              << " complete=" << (manifest_complete_ok ? "PASS" : "FAIL")
              << " rk_time="
              << (manifest_time_domain_ok ? "PASS" : "FAIL")
              << " unix_epoch=" << (manifest_epoch_ok ? "PASS" : "FAIL")
              << " alignment="
              << (manifest_alignment_ok ? "PASS" : "FAIL")
              << " unsynced_drops="
              << (strict_time_drop_counts_ok ? "PASS" : "FAIL")
              << " lidar_v6_source="
              << (lidar_v6_source_ok ? "PASS" : "FAIL")
              << " lidar_imu_v6_source="
              << (lidar_imu_v6_source_ok ? "PASS" : "FAIL")
              << " browser_load=" << (browser_load_ok ? "PASS" : "FAIL")
              << " validation="
              << (recorded_validation_ok ? "PASS" : "FAIL")
              << " no_lidar=" << (no_lidar_ok ? "PASS" : "FAIL")
              << " queue_order="
              << (writer_queue_order_ok ? "PASS" : "FAIL")
              << " empty_strict="
              << (unsynced_only_fails ? "PASS" : "FAIL")
              << " in_progress="
              << (in_progress_manifest_ok ? "PASS" : "FAIL")
              << " error=" << summary.error << "\n";
    return success ? 0 : 11;
  }
  QSettings language_settings(QStringLiteral("DIBULI"),
                              QStringLiteral("PrismViewer"));
  QString language = qEnvironmentVariable("PRISM_VIEWER_LANG");
  if (language.isEmpty()) {
    language = language_settings.value(QStringLiteral("language")).toString();
  }
  if (language.isEmpty()) {
    common::setChineseUi(
        QLocale::system().language() == QLocale::Chinese);
  } else {
    common::setChineseUi(
        language.startsWith(QStringLiteral("zh"), Qt::CaseInsensitive));
  }
  MainWindow window;
  if (command_line.contains(QStringLiteral("--window-layout-self-test"))) {
    window.ensurePolished();
    if (window.centralWidget() != nullptr &&
        window.centralWidget()->layout() != nullptr) {
      window.centralWidget()->layout()->activate();
    }
    const QSize minimum =
        window.minimumSizeHint().expandedTo(window.minimumSize());
    const auto* imu_alignment_button = window.findChild<QPushButton*>(
        QStringLiteral("datasetImuAlignmentButton"));
    const bool success =
        minimum.width() <= kMaximumMainWindowMinimumWidth &&
        minimum.height() <= kMaximumMainWindowMinimumHeight &&
        imu_alignment_button != nullptr;
    std::cout << "main_window_layout_self_test="
              << (success ? "PASS" : "FAIL")
              << " minimum=" << minimum.width() << "x"
              << minimum.height() << " limit="
              << kMaximumMainWindowMinimumWidth << "x"
              << kMaximumMainWindowMinimumHeight
              << " imu_alignment_button="
              << (imu_alignment_button != nullptr ? "PASS" : "FAIL")
              << "\n";
    return success ? 0 : 12;
  }
  window.show();
  return app.exec();
}

}  // namespace prism_viewer
