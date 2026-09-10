#pragma once

#include "stream/stream_source.hpp"

#include "common/sample_rate_tracker.hpp"
#include "communication/device_info_compat.hpp"
#include "communication/prism_runtime.hpp"
#include "transfer/camera_frame_assembler.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace prism_viewer::stream {

// Configuration for the USB stream source.
struct UsbStreamSourceConfig {
  // LiDAR model to use (None, Mid360, Mid360S).
  prism::LidarModel lidar_model = prism::LidarModel::None;

  // Whether to record LiDAR streams.
  bool record_lidar = false;

  // Camera frame rate (0 = use device default).
  uint32_t camera_fps = 0;

  // Whether to enable rover RTCM stream.
  bool enable_rover_rtcm = true;

  // Whether to enable CORS session.
  bool cors_active = false;

  // Watchdog timeout for camera frame-set progress.
  std::chrono::milliseconds camera_progress_timeout{5000};

  // Period for device info polling.
  std::chrono::milliseconds device_info_period{500};

  // Period for GNSS timing query.
  std::chrono::milliseconds gnss_timing_period{100};
};

// USB device data source. Wraps the Prism SDK client and streams, delivering
// sensor data via callbacks. Runs a background thread for frame reception.
//
// This class handles only low-level stream parsing. All UI updates and
// recording are done by the consumer via the callback functions.
class UsbStreamSource : public StreamSource {
 public:
  // `client_io_mutex` must be the same mutex every other user of `client`
  // locks. The SDK client is not thread-safe, and the worker thread reads
  // frames while the UI thread issues device commands on it.
  UsbStreamSource(prism_runtime::Client& client, std::mutex& client_io_mutex);
  ~UsbStreamSource() override;

  // Non-copyable, non-movable.
  UsbStreamSource(const UsbStreamSource&) = delete;
  UsbStreamSource& operator=(const UsbStreamSource&) = delete;

  // Configure before start().
  void set_config(UsbStreamSourceConfig config);

  // --- StreamSource interface ---

  bool start() override;
  void stop() override;
  bool active() const override;
  std::string status() const override;

  // USB-specific capabilities.
  bool can_record() const override { return true; }
  bool can_control_exposure() const override { return true; }
  bool can_control_wifi() const override { return true; }
  bool can_sync_time() const override { return true; }

  std::optional<prism::DeviceInfo> device_info() const override;
  communication::TimeSyncProvider time_sync_provider() const override {
    return time_sync_provider_;
  }

  // Access to the underlying client (for device commands).
  prism_runtime::Client& client() { return client_; }

  // Execute a client I/O operation with proper locking.
  template <typename Function>
  auto with_client_io(Function&& function) -> std::invoke_result_t<Function&> {
    std::lock_guard<std::mutex> lock(client_io_mutex_);
    return function();
  }

  // Query device info (thread-safe).
  communication::DeviceInfoStatus query_device_info();

  // Query GNSS timing status (thread-safe).
  prism::GnssTimingStatus query_gnss_timing();

  // Query LiDAR status (thread-safe).
  prism::LidarStatus query_lidar_status();

  // Send video ACK (thread-safe).
  void send_video_ack(uint32_t frame_id);

  // Stop video (thread-safe).
  void stop_video();

  // Get the current camera progress age.
  std::chrono::milliseconds camera_progress_age() const;

  // Update the camera progress timestamp (compensate for command time).
  void touch_camera_progress();

 private:
  // The main worker thread function.
  void worker_main();

  // Dispatch a frame to the appropriate stream handler.
  bool dispatch_frame(const prism::Frame& frame);

  // Handle a completed camera frame set.
  void handle_camera_frame(transfer::CameraFrameSet frame);

  // Handle a video chunk.
  void handle_video_chunk(const prism::VideoChunkView& chunk);

  // Handle video metadata.
  void handle_video_meta(const prism::VideoMeta& meta);

  // Handle a heartbeat frame.
  void handle_heartbeat(const prism::HeartbeatStatus& heartbeat);

  // Handle a device info response.
  void handle_device_info_response(const prism::DeviceInfo& info,
                                   communication::TimeSyncProvider provider);

  // Handle GNSS timing status.
  void handle_gnss_timing(const prism::GnssTimingStatus& status);

  // Handle LiDAR status.
  void handle_lidar_status(const prism::LidarStatus& status);

  // Log a message (thread-safe, posts to UI).
  void log(const std::string& message);

  // Check if camera frame-set progress has stalled.
  bool is_camera_progress_stalled(std::chrono::steady_clock::time_point now);

  // Per-sensor IMU bookkeeping: counters, throttling and the timestamp
  // continuity check that raises on_imu_timestamp_alarm.
  void handle_imu_sample(const prism::ImuSample& sample);

  // Continuity state for one IMU. A synchronized FSYNC sample is a precise PPS
  // re-anchor, so it may legitimately step the clock; see imu_timestamp_policy.
  struct TimestampCheck {
    bool initialized = false;
    bool alarm = false;
    bool last_timestamp_synced = false;
    uint64_t last_timestamp_us = 0;
    uint16_t last_sequence = 0;
    uint32_t bad_streak = 0;
    uint32_t good_streak = 0;
  };

  void check_imu_timestamp(const prism::ImuSample& sample, int sensor);

  // Member variables.

  prism_runtime::Client& client_;
  UsbStreamSourceConfig config_;

  std::mutex& client_io_mutex_;
  std::thread worker_thread_;
  std::atomic<bool> active_{false};
  std::atomic<bool> stop_requested_{false};

  // Streams.
  std::optional<prism_runtime::ImuStream> imu_stream_;
  std::optional<prism_runtime::LidarStream> lidar_stream_;
  std::optional<prism_runtime::RoverRtcmStream> rover_rtcm_stream_;

  // Camera frame assembler.
  transfer::CameraFrameAssembler camera_assembler_;

  // IMU bookkeeping (worker thread only).
  std::array<common::SampleRateTracker, 2> imu_rate_{
      common::SampleRateTracker(std::chrono::seconds(5)),
      common::SampleRateTracker(std::chrono::seconds(5))};
  std::array<uint64_t, 2> imu_sample_counts_{};
  std::array<uint64_t, 2> imu_fsync_counts_{};
  std::array<uint64_t, 2> imu_last_fsync_sample_us_{};
  std::array<bool, 2> imu_last_fsync_delay_valid_{};
  std::array<TimestampCheck, 2> imu_timestamp_checks_{};
  std::array<std::chrono::steady_clock::time_point, 2> next_imu_ui_post_{};
  std::array<std::chrono::steady_clock::time_point, 2> next_imu_plot_post_{};

  // LiDAR preview accumulation (worker thread only).
  std::vector<prism::LidarPoint> pending_lidar_preview_;
  uint64_t received_lidar_points_ = 0;
  std::chrono::steady_clock::time_point next_lidar_preview_post_{};

  // Camera statistics (worker thread only). The 2 s window matches the frame
  // rate shown in the status line; 5 s for IMU smooths its much higher rate.
  common::SampleRateTracker camera_rate_{std::chrono::seconds(2)};
  uint64_t received_camera_frame_sets_ = 0;
  std::chrono::steady_clock::time_point next_video_meta_post_{};

  // State.
  std::chrono::steady_clock::time_point last_camera_frame_time_;
  std::chrono::steady_clock::time_point last_usb_frame_time_;
  std::chrono::steady_clock::time_point last_video_chunk_time_;
  std::chrono::steady_clock::time_point next_device_info_query_;
  std::chrono::steady_clock::time_point next_gnss_timing_query_;

  // Device info.
  std::optional<prism::DeviceInfo> device_info_;
  communication::TimeSyncProvider time_sync_provider_ =
      communication::TimeSyncProvider::Unsynced;

  // Status.
  mutable std::mutex status_mutex_;
  std::string status_;
};

}  // namespace prism_viewer::stream
