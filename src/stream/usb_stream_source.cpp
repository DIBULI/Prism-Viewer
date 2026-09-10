#include "stream/usb_stream_source.hpp"

#include "communication/device_info_compat.hpp"
#include "communication/prism_runtime.hpp"
#include "imu_timestamp_policy.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <exception>
#include <functional>
#include <iomanip>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace prism_viewer::stream {
namespace {

// Re-export constants from main_window.cpp for consistency.
constexpr auto kCameraFrameSetProgressTimeout = std::chrono::seconds(5);
constexpr auto kCameraControlCommandFreshnessLimit =
    std::chrono::milliseconds(1500);

// UI refresh throttling. Sensor data arrives far faster than the display can
// use it, so these bound how often the worker posts to the consumer.
constexpr auto kImuUiPeriod = std::chrono::milliseconds(20);
constexpr auto kImuPlotSamplePeriod = std::chrono::milliseconds(10);
constexpr auto kMetadataUiPeriod = std::chrono::milliseconds(200);
constexpr auto kLidarPreviewPeriod = std::chrono::milliseconds(50);
constexpr size_t kLidarPreviewReserve = 8192u;

// Sampling-interval bounds for the IMU timestamp continuity check, in
// microseconds. Outside these the stream is treated as faulty.
constexpr uint64_t kImuIntervalMinUs = 250u;
constexpr uint64_t kImuIntervalMaxUs = 4000u;
constexpr uint64_t kImuIntervalSevereUs = 10000u;

// Hysteresis so a single glitch does not flap the alarm.
constexpr uint32_t kImuBadStreakToAlarm = 3u;
constexpr uint32_t kImuGoodStreakToClear = 1000u;

// A transient USB read failure is tolerated while the agent keepalive proves
// the link is still up; without keepalive the first failure is fatal.
constexpr int kMaxConsecutiveUsbReadErrors = 3;

}  // namespace

// --- UsbStreamSource Implementation ---

UsbStreamSource::UsbStreamSource(prism_runtime::Client& client,
                                 std::mutex& client_io_mutex)
    : client_(client), client_io_mutex_(client_io_mutex) {
  status_ = "idle";
}

UsbStreamSource::~UsbStreamSource() {
  if (active_.load()) {
    stop();
  }
}

void UsbStreamSource::set_config(UsbStreamSourceConfig config) {
  config_ = config;
}

bool UsbStreamSource::start() {
  if (active_.load()) {
    log("USB source already active");
    return false;
  }

  if (!client_.isOpen()) {
    log("USB source: device not open");
    return false;
  }

  stop_requested_.store(false);
  active_.store(true);
  status_ = "starting";

  // Initialize timestamps.
  const auto now = std::chrono::steady_clock::now();
  last_camera_frame_time_ = now;
  last_usb_frame_time_ = now;
  last_video_chunk_time_ = now;
  next_device_info_query_ = now + config_.device_info_period;
  next_gnss_timing_query_ = now + config_.gnss_timing_period;

  // Reset per-session bookkeeping so a second capture does not inherit the
  // counters, rate history or alarm state of the previous one.
  imu_rate_.fill(common::SampleRateTracker(std::chrono::seconds(5)));
  imu_sample_counts_.fill(0);
  imu_fsync_counts_.fill(0);
  imu_last_fsync_sample_us_.fill(0);
  imu_last_fsync_delay_valid_.fill(false);
  imu_timestamp_checks_.fill(TimestampCheck{});
  next_imu_ui_post_.fill(now);
  next_imu_plot_post_.fill(now);
  pending_lidar_preview_.clear();
  pending_lidar_preview_.reserve(8192u);
  received_lidar_points_ = 0;
  next_lidar_preview_post_ = now;
  camera_rate_ = common::SampleRateTracker(std::chrono::seconds(2));
  received_camera_frame_sets_ = 0;
  next_video_meta_post_ = now;

  // Start worker thread.
  worker_thread_ = std::thread([this] { worker_main(); });

  return true;
}

void UsbStreamSource::stop() {
  if (!active_.load() && !worker_thread_.joinable()) {
    return;
  }

  stop_requested_.store(true);
  active_.store(false);

  if (worker_thread_.joinable()) {
    worker_thread_.join();
  }

  // Reset streams.
  imu_stream_.reset();
  lidar_stream_.reset();
  rover_rtcm_stream_.reset();
  camera_assembler_.reset();

  status_ = "stopped";
  log("USB source stopped");
}

bool UsbStreamSource::active() const {
  return active_.load();
}

std::string UsbStreamSource::status() const {
  std::lock_guard<std::mutex> lock(status_mutex_);
  return status_;
}

std::optional<prism::DeviceInfo> UsbStreamSource::device_info() const {
  return device_info_;
}

communication::DeviceInfoStatus UsbStreamSource::query_device_info() {
  return with_client_io(
      [this]() { return communication::readDeviceInfo(client_); });
}

prism::GnssTimingStatus UsbStreamSource::query_gnss_timing() {
  return with_client_io(
      [this]() { return client_.gnssTimingStatus(); });
}

prism::LidarStatus UsbStreamSource::query_lidar_status() {
  return with_client_io(
      [this]() { return client_.lidarStatus(); });
}

void UsbStreamSource::send_video_ack(uint32_t frame_id) {
  with_client_io([this, frame_id]() { client_.sendVideoAck(frame_id); });
}

void UsbStreamSource::stop_video() {
  with_client_io([this]() { client_.stopVideo(); });
}

std::chrono::milliseconds UsbStreamSource::camera_progress_age() const {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - last_camera_frame_time_);
}

void UsbStreamSource::touch_camera_progress() {
  last_camera_frame_time_ = std::chrono::steady_clock::now();
}

// --- Worker Thread ---

void UsbStreamSource::worker_main() {
  bool video_started = false;
  bool aggregate_stream_start_attempted = false;
  bool aggregate_stream_stop_attempted = false;

  try {
    if (!client_.isOpen()) {
      throw std::runtime_error("device is not open");
    }

    // Wait for sensor-board link.
    bool sensor_board_link_ready = false;
    std::optional<prism::DeviceInfo> capture_device_info;
    std::optional<std::chrono::steady_clock::time_point>
        capture_device_info_at;

    while (!stop_requested_.load() && !sensor_board_link_ready) {
      if (on_poll_tick) on_poll_tick();
      try {
        const auto status = with_client_io(
            [this]() { return communication::readDeviceInfo(client_); });
        capture_device_info = status.info;
        capture_device_info_at = std::chrono::steady_clock::now();
        handle_device_info_response(status.info, status.time_sync_provider);
        handle_gnss_timing(with_client_io(
            [this]() { return client_.gnssTimingStatus(); }));
        sensor_board_link_ready = status.info.sensor_board_online;
      } catch (const std::exception&) {
        // Ignore and retry.
      }

      if (!stop_requested_.load() && !sensor_board_link_ready) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
      }
    }

    if (!sensor_board_link_ready) {
      log("Capture cancelled while waiting for RK/sensor-board link");
      active_.store(false);
      return;
    }

    log("RK/sensor-board link online; starting camera and IMU streams");

    // Start video.
    aggregate_stream_start_attempted = true;
    const auto video_status = with_client_io(
        [this]() { return client_.startVideo1280x1024(); });
    video_started = true;
    log("Video started cameras=" + std::to_string(video_status.cameras) +
        " fps=" + std::to_string(video_status.fps));

    // Camera frame tracking.
    auto last_completed_camera_frame_set_at =
        std::chrono::steady_clock::now();
    auto last_usb_frame_at = last_completed_camera_frame_set_at;
    auto last_video_chunk_at = last_completed_camera_frame_set_at;

    // Create IMU stream.
    imu_stream_.emplace(
        client_,
        [this](const prism::ImuSample& sample) { handle_imu_sample(sample); });

    // Create LiDAR stream.
    lidar_stream_.emplace(
        client_,
        [this](const prism::LidarPointBatch& batch) {
          if (on_lidar) on_lidar(batch);

          // Batches arrive far faster than the 3D view can redraw, so points
          // accumulate here and are handed over at a bounded rate.
          if (!on_lidar_preview || batch.points.empty()) return;
          received_lidar_points_ += batch.points.size();
          pending_lidar_preview_.insert(pending_lidar_preview_.end(),
                                        batch.points.begin(),
                                        batch.points.end());
          const auto now = std::chrono::steady_clock::now();
          if (now < next_lidar_preview_post_) return;
          next_lidar_preview_post_ = now + kLidarPreviewPeriod;
          on_lidar_preview(std::move(pending_lidar_preview_),
                           config_.lidar_model, received_lidar_points_,
                           batch.batch_id, batch.timestamp_synced);
          pending_lidar_preview_.clear();
          pending_lidar_preview_.reserve(kLidarPreviewReserve);
        },
        [this](const prism::LidarImuSample& sample) {
          if (on_lidar_imu) on_lidar_imu(sample);
        });

    // Create rover RTCM stream.
    rover_rtcm_stream_.emplace(
        client_, [this](const prism::RoverRtcmChunkView& chunk) {
          if (on_rover_rtcm) on_rover_rtcm(chunk);
        });

    // Start streams.
    try {
      with_client_io([this]() { imu_stream_->start(); });
    } catch (...) {
      if (video_started) {
        try {
          aggregate_stream_stop_attempted = true;
          with_client_io([this]() { client_.stopVideo(); });
          video_started = false;
        } catch (const std::exception& stop_error) {
          log("Capture rollback failed after IMU start error: " +
              std::string(stop_error.what()));
        }
      }
      throw;
    }
    log("IMU started through agent SDK ImuStream");

    if (config_.lidar_model != prism::LidarModel::None) {
      try {
        with_client_io([this]() { lidar_stream_->start(config_.lidar_model); });
        handle_lidar_status(with_client_io(
            [this]() { return client_.lidarStatus(); }));
        log("LiDAR started model=" +
            std::to_string(static_cast<int>(config_.lidar_model)));
      } catch (...) {
        try {
          aggregate_stream_stop_attempted = true;
          with_client_io([this]() { imu_stream_->stop(); });
          video_started = false;
        } catch (const std::exception& stop_error) {
          log("Capture rollback failed after LiDAR start error: " +
              std::string(stop_error.what()));
        }
        throw;
      }
    }

    with_client_io([this]() { rover_rtcm_stream_->start(); });
    log("Rover RTCM stream started");

    // Main capture loop.
    const auto capture_started_at = std::chrono::steady_clock::now();
    last_completed_camera_frame_set_at = capture_started_at;
    last_usb_frame_at = capture_started_at;
    last_video_chunk_at = capture_started_at;
    auto next_device_info_query =
        capture_started_at + std::chrono::milliseconds(500);

    std::exception_ptr capture_error;
    int consecutive_usb_read_errors = 0;
    try {
      while (!stop_requested_.load()) {
        auto loop_now = std::chrono::steady_clock::now();

        // Check camera progress.
        if (is_camera_progress_stalled(loop_now)) {
          throw std::runtime_error(
              "Camera frame-set progress stalled: no complete four-camera "
              "frame set for " +
              std::to_string(
                  std::chrono::duration_cast<std::chrono::milliseconds>(
                      loop_now - last_completed_camera_frame_set_at).count()) +
              " ms");
        }

        // Device commands that must be issued from the capture thread are
        // pumped here. A live exposure transaction can fill the SDK deferred
        // frame queue, so when progress was fresh before the command ran, the
        // gap it caused is forgiven rather than tripping the stall watchdog.
        if (on_poll_tick) {
          const auto progress_age_before = loop_now -
                                           last_completed_camera_frame_set_at;
          if (on_poll_tick()) {
            loop_now = std::chrono::steady_clock::now();
            if (progress_age_before <= kCameraControlCommandFreshnessLimit) {
              last_completed_camera_frame_set_at = loop_now;
            }
          }
        }

        // Periodic device info query.
        if (loop_now >= next_device_info_query) {
          const auto query_started_at = std::chrono::steady_clock::now();
          try {
            const auto status = with_client_io(
                [this]() { return communication::readDeviceInfo(client_); });
            capture_device_info = status.info;
            capture_device_info_at = std::chrono::steady_clock::now();
            handle_device_info_response(status.info, status.time_sync_provider);
            handle_gnss_timing(with_client_io(
                [this]() { return client_.gnssTimingStatus(); }));

            if (!status.info.sensor_board_online) {
              log("DeviceInfo reports sensor-board offline; stopping");
              stop_requested_.store(true);
              break;
            }

            if (config_.lidar_model != prism::LidarModel::None) {
              try {
                handle_lidar_status(with_client_io(
                    [this]() { return client_.lidarStatus(); }));
              } catch (const std::exception& lidar_error) {
                log("LiDAR status refresh failed: " +
                    std::string(lidar_error.what()));
              }
            }
          } catch (const std::exception& ex) {
            log("DeviceInfo refresh failed: " + std::string(ex.what()));
          }
          const auto query_finished_at = std::chrono::steady_clock::now();
          last_completed_camera_frame_set_at +=
              query_finished_at - query_started_at;
          next_device_info_query =
              query_finished_at + std::chrono::seconds(1);
        }

        // Read frame. A transient failure is survivable while the agent
        // keepalive still proves the link is up; without it, or once the
        // failures pile up, the transport is genuinely gone.
        prism::Frame frame;
        try {
          frame = with_client_io([this]() {
            return client_.readFrame(config_.cors_active ? 100u : 1000u);
          });
        } catch (const std::exception& ex) {
          ++consecutive_usb_read_errors;
          if (consecutive_usb_read_errors == 1) {
            log("USB frame read failed: " + std::string(ex.what()));
          }
          const bool keepalive_proves_link =
              with_client_io([this]() { return client_.keepaliveEnabled(); });
          if (!keepalive_proves_link ||
              consecutive_usb_read_errors >= kMaxConsecutiveUsbReadErrors) {
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

        // Dispatch frame.
        if (dispatch_frame(frame)) {
          continue;
        }

        // Handle heartbeat.
        if (frame.type == prism::FrameType::Heartbeat) {
          const auto heartbeat = prism_runtime::parseHeartbeat(frame);
          handle_heartbeat(heartbeat);
        } else if (frame.type == prism::FrameType::VideoChunk) {
          const auto chunk = prism_runtime::parseVideoChunkView(frame);
          handle_video_chunk(chunk);
        } else if (frame.type == prism::FrameType::VideoMeta) {
          const auto meta = prism_runtime::parseVideoMeta(frame);
          handle_video_meta(meta);
        }
      }
    } catch (...) {
      capture_error = std::current_exception();
    }

    // Stop streams.
    std::exception_ptr rover_rtcm_stop_error;
    try {
      with_client_io([this]() { rover_rtcm_stream_->stop(); });
    } catch (...) {
      rover_rtcm_stop_error = std::current_exception();
    }

    std::exception_ptr lidar_stop_error;
    try {
      with_client_io([this]() { lidar_stream_->stop(); });
    } catch (...) {
      lidar_stop_error = std::current_exception();
    }

    std::exception_ptr stop_error;
    aggregate_stream_stop_attempted = true;
    try {
      with_client_io([this]() { imu_stream_->stop(); });
      video_started = false;
    } catch (...) {
      stop_error = std::current_exception();
    }

    if (capture_error) {
      if (rover_rtcm_stop_error) {
        try {
          std::rethrow_exception(rover_rtcm_stop_error);
        } catch (const std::exception& ex) {
          log("Capture error cleanup could not confirm rover RTCM stop: " +
              std::string(ex.what()));
        }
      }
      if (lidar_stop_error) {
        try {
          std::rethrow_exception(lidar_stop_error);
        } catch (const std::exception& ex) {
          log("Capture error cleanup could not confirm LiDAR stop: " +
              std::string(ex.what()));
        }
      }
      if (stop_error) {
        try {
          std::rethrow_exception(stop_error);
        } catch (const std::exception& ex) {
          log("Capture error cleanup could not confirm aggregate stream stop: " +
              std::string(ex.what()));
        }
      }
      std::rethrow_exception(capture_error);
    }

    if (rover_rtcm_stop_error) std::rethrow_exception(rover_rtcm_stop_error);
    if (lidar_stop_error) std::rethrow_exception(lidar_stop_error);
    if (stop_error) std::rethrow_exception(stop_error);

    log("Streams stopped");
    status_ = "stopped";

  } catch (const std::exception& ex) {
    log("Error: " + std::string(ex.what()));
    status_ = "error: " + std::string(ex.what());

    if (aggregate_stream_start_attempted &&
        !aggregate_stream_stop_attempted && client_.isOpen()) {
      aggregate_stream_stop_attempted = true;
      try {
        with_client_io([this]() { client_.stopVideo(); });
        log("Capture error rollback stopped camera and IMU streams");
      } catch (const std::exception& stop_error) {
        log("Capture error rollback could not confirm stream stop: " +
            std::string(stop_error.what()));
      }
    }
  }

  if (on_capture_finished) {
    on_capture_finished(
        "Capture stopped before the exposure request was processed");
  }

  active_.store(false);
}

// --- Frame Dispatch ---

bool UsbStreamSource::dispatch_frame(const prism::Frame& frame) {
  if (imu_stream_ && imu_stream_->handleFrame(frame)) {
    return true;
  }
  if (lidar_stream_ && lidar_stream_->handleFrame(frame)) {
    return true;
  }
  if (rover_rtcm_stream_ && rover_rtcm_stream_->handleFrame(frame)) {
    return true;
  }
  return false;
}

// --- Frame Handlers ---

void UsbStreamSource::handle_video_chunk(const prism::VideoChunkView& chunk) {
  last_video_chunk_time_ = std::chrono::steady_clock::now();
  auto result = camera_assembler_.ingest(chunk);

  for (uint32_t discarded : result.discarded_incomplete_frame_ids) {
    send_video_ack(discarded);
  }

  if (result.completed.has_value()) {
    handle_camera_frame(std::move(*result.completed));
  }
}

void UsbStreamSource::handle_imu_sample(const prism::ImuSample& sample) {
  const int sensor = static_cast<int>(sample.sensor_id);
  if (sensor < 0 || sensor >= static_cast<int>(imu_sample_counts_.size())) {
    return;
  }

  if (on_imu) on_imu(sample);

  const uint64_t received_count = ++imu_sample_counts_[sensor];
  if (sample.fsync_event) {
    ++imu_fsync_counts_[sensor];
    imu_last_fsync_sample_us_[sensor] = sample.timestamp_us;
    imu_last_fsync_delay_valid_[sensor] = sample.fsync_delay_valid;
  }

  const auto now = std::chrono::steady_clock::now();
  imu_rate_[sensor].add(now);

  if (on_imu_plot && now >= next_imu_plot_post_[sensor]) {
    next_imu_plot_post_[sensor] = now + kImuPlotSamplePeriod;
    on_imu_plot(sample, now);
  }

  check_imu_timestamp(sample, sensor);

  if (on_imu_ui && now >= next_imu_ui_post_[sensor]) {
    next_imu_ui_post_[sensor] = now + kImuUiPeriod;
    on_imu_ui(sample, received_count, imu_rate_[sensor].rate(now),
              imu_fsync_counts_[sensor], imu_last_fsync_sample_us_[sensor],
              imu_last_fsync_delay_valid_[sensor]);
  }
}

void UsbStreamSource::check_imu_timestamp(const prism::ImuSample& sample,
                                          int sensor) {
  auto& check = imu_timestamp_checks_[sensor];
  const uint16_t current_sequence =
      static_cast<uint16_t>(sample.sample_id & 0xffffu);
  const bool timestamp_valid = sample.timestamp_us != 0;
  const bool timestamp_domain_changed =
      check.initialized &&
      sample.timestamp_synced != check.last_timestamp_synced;
  const uint16_t sequence_delta =
      check.initialized
          ? static_cast<uint16_t>(current_sequence - check.last_sequence)
          : 0u;
  const bool expected_fsync_reanchor =
      shouldRebaselineForSyncedFsync(check.initialized, sample.timestamp_synced,
                                     sample.fsync_event,
                                     sample.fsync_delay_valid,
                                     sample.sample_gap, sequence_delta) ||
      shouldRebaselineForFirstUtcFsync(
          check.initialized, sample.timestamp_synced, sample.fsync_event,
          sample.fsync_delay_valid, sample.sample_gap, sequence_delta,
          check.last_timestamp_us, sample.timestamp_us);

  const auto clear_alarm = [this, sensor, &check]() {
    if (!check.alarm) return;
    check.alarm = false;
    if (on_imu_timestamp_alarm) {
      on_imu_timestamp_alarm(sensor, false, std::string());
    }
  };

  // Local sensor-board time and synchronized UTC are different time domains.
  // Every valid synchronized FSYNC sample can replace the extrapolated sample
  // clock with the precise PPS anchor. When the sequence is continuous and PL
  // reports no sample gap, that clock correction is not a sampling-interval
  // failure. Establish a fresh baseline and check again from the next sample.
  if (!timestamp_valid || timestamp_domain_changed || expected_fsync_reanchor) {
    check.bad_streak = 0;
    // An expected periodic re-anchor must not conceal an alarm raised by an
    // actual earlier stream fault, nor interrupt its run of good samples
    // toward recovery.
    if (!expected_fsync_reanchor) {
      check.good_streak = 0;
      clear_alarm();
    }
  } else if (check.initialized) {
    const bool timestamp_regressed =
        sample.timestamp_us <= check.last_timestamp_us;
    const bool sequence_repeated = sequence_delta == 0;
    const uint64_t total_delta_us =
        timestamp_regressed ? 0 : sample.timestamp_us - check.last_timestamp_us;
    const uint64_t interval_us =
        sequence_repeated
            ? 0
            : (total_delta_us + sequence_delta / 2u) / sequence_delta;
    const bool interval_bad = timestamp_regressed || sequence_repeated ||
                              interval_us < kImuIntervalMinUs ||
                              interval_us > kImuIntervalMaxUs;
    if (interval_bad) {
      check.good_streak = 0;
      ++check.bad_streak;
      const bool severe = timestamp_regressed || sequence_repeated ||
                          interval_us > kImuIntervalSevereUs;
      if (!check.alarm &&
          (severe || check.bad_streak >= kImuBadStreakToAlarm)) {
        check.alarm = true;
        if (on_imu_timestamp_alarm) {
          std::ostringstream detail;
          if (timestamp_regressed) {
            detail << "regression/repeat: previous=" << check.last_timestamp_us
                   << " us current=" << sample.timestamp_us << " us";
          } else if (sequence_repeated) {
            detail << "duplicate sample detected";
          } else {
            detail << "delta=" << total_delta_us << " us over "
                   << sequence_delta << " samples (" << interval_us
                   << " us/sample)";
          }
          detail << " | fsync=" << (sample.fsync_event ? 1 : 0)
                 << " synced=" << (sample.timestamp_synced ? 1 : 0)
                 << " flags=0x" << std::hex << std::setw(4)
                 << std::setfill('0') << sample.flags << std::dec
                 << " gap=" << (sample.sample_gap ? 1 : 0);
          on_imu_timestamp_alarm(sensor, true, detail.str());
        }
      }
    } else {
      check.bad_streak = 0;
      if (check.alarm) {
        ++check.good_streak;
        if (check.good_streak >= kImuGoodStreakToClear) {
          check.good_streak = 0;
          clear_alarm();
        }
      }
    }
  }

  if (timestamp_valid) {
    check.initialized = true;
    check.last_timestamp_us = sample.timestamp_us;
    check.last_sequence = current_sequence;
    check.last_timestamp_synced = sample.timestamp_synced;
  } else {
    check.initialized = false;
  }
}

void UsbStreamSource::handle_video_meta(const prism::VideoMeta& meta) {
  auto completed = camera_assembler_.addMetadata(meta);
  if (completed.has_value()) {
    handle_camera_frame(std::move(*completed));
  }

  // The diagnostics panel only needs an occasional refresh; metadata arrives
  // once per frame set.
  const auto now = std::chrono::steady_clock::now();
  if (on_video_meta && now >= next_video_meta_post_) {
    next_video_meta_post_ = now + kMetadataUiPeriod;
    on_video_meta(meta);
  }
}

void UsbStreamSource::handle_camera_frame(transfer::CameraFrameSet frame) {
  const auto now = std::chrono::steady_clock::now();
  last_camera_frame_time_ = now;

  ++received_camera_frame_sets_;
  camera_rate_.add(now);

  if (on_camera_frame_set_status) {
    std::array<size_t, 4> jpeg_sizes{};
    for (size_t camera = 0; camera < jpeg_sizes.size(); ++camera) {
      jpeg_sizes[camera] = frame.jpeg[camera].size();
    }
    on_camera_frame_set_status(frame.frame_id, received_camera_frame_sets_,
                               camera_rate_.rate(now), jpeg_sizes,
                               frame.metadata.exposure_us);
  }

  if (on_camera) {
    on_camera(std::move(frame));
  }
}

void UsbStreamSource::handle_heartbeat(const prism::HeartbeatStatus& heartbeat) {
  if (on_heartbeat) {
    on_heartbeat(heartbeat);
  }
}

void UsbStreamSource::handle_device_info_response(
    const prism::DeviceInfo& info,
    communication::TimeSyncProvider provider) {
  device_info_ = info;
  time_sync_provider_ = provider;
  if (on_device_info) {
    on_device_info(info, provider);
  }
}

void UsbStreamSource::handle_gnss_timing(
    const prism::GnssTimingStatus& status) {
  if (on_gnss_timing) {
    on_gnss_timing(status);
  }
}

void UsbStreamSource::handle_lidar_status(const prism::LidarStatus& status) {
  if (on_lidar_status) {
    on_lidar_status(status);
  }
}

// --- Helpers ---

void UsbStreamSource::log(const std::string& message) {
  if (on_log) {
    on_log(message);
  }
}

bool UsbStreamSource::is_camera_progress_stalled(
    std::chrono::steady_clock::time_point now) {
  const auto age = now - last_camera_frame_time_;
  return age >= config_.camera_progress_timeout;
}

}  // namespace prism_viewer::stream
