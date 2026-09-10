#pragma once

#include "communication/device_info_compat.hpp"
#include "prism/usb/telemetry.hpp"
#include "transfer/camera_frame_assembler.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace prism_viewer::stream {

// Abstract data source for the Viewer. UsbStreamSource is currently the only
// implementation. Data callbacks fire on the source's worker thread; the
// consumer marshals to the Qt main thread through post().
class StreamSource {
 public:
  virtual ~StreamSource() = default;

  // Start streaming. Returns true on success.
  virtual bool start() = 0;

  // Stop streaming. Must be thread-safe and idempotent.
  virtual void stop() = 0;

  // Whether the source is currently streaming.
  virtual bool active() const = 0;

  // Human-readable status for the UI.
  virtual std::string status() const = 0;

  // --- Data callbacks (set by consumer, called by source) ---

  // IMU sample from onboard sensor board (sensor_id 0 or 1). Fires for every
  // sample; use the throttled callbacks below to drive UI.
  std::function<void(const prism::ImuSample&)> on_imu;

  // Throttled IMU sample for the live plot, carrying the receive time so the
  // plot keeps a consistent x-axis when the queue drains in bursts.
  std::function<void(const prism::ImuSample&,
                     std::chrono::steady_clock::time_point)>
      on_imu_plot;

  // Throttled IMU statistics for the sensor table.
  std::function<void(const prism::ImuSample& sample, uint64_t received_count,
                     double sample_rate_hz, uint64_t fsync_event_count,
                     uint64_t last_fsync_sample_us,
                     bool last_fsync_delay_valid)>
      on_imu_ui;

  // Raised when the IMU timestamp stream stops looking continuous, and cleared
  // once it recovers. `detail` is empty when clearing.
  std::function<void(int sensor, bool active, const std::string& detail)>
      on_imu_timestamp_alarm;

  // LiDAR point cloud batch.
  std::function<void(const prism::LidarPointBatch&)> on_lidar;

  // Accumulated and throttled LiDAR points for the 3D preview. The source
  // batches points between calls so the UI thread is not flooded.
  std::function<void(std::vector<prism::LidarPoint> points,
                     prism::LidarModel model, uint64_t total_points,
                     uint32_t batch_id, bool timestamp_synced)>
      on_lidar_preview;

  // LiDAR built-in IMU sample.
  std::function<void(const prism::LidarImuSample&)> on_lidar_imu;

  // Complete four-camera frame set.
  std::function<void(transfer::CameraFrameSet)> on_camera;

  // Per-frame-set statistics for the camera status line.
  std::function<void(uint32_t frame_id, uint64_t received_frame_sets,
                     double received_fps,
                     const std::array<size_t, 4>& jpeg_sizes,
                     const std::array<uint32_t, 4>& exposure_us)>
      on_camera_frame_set_status;

  // Throttled video metadata for the diagnostics panel.
  std::function<void(const prism::VideoMeta&)> on_video_meta;

  // Called from the worker loop while capture runs, so the consumer can pump
  // device operations that must be issued from the capture thread (camera
  // exposure changes). Returns true when an operation was processed, which
  // tells the source to forgive the frame-set progress gap the command caused.
  std::function<bool()> on_poll_tick;

  // Called once when capture ends, so the consumer can cancel any device
  // operation still queued. `reason` is human-readable.
  std::function<void(const std::string& reason)> on_capture_finished;

  // Rover RTCM chunk (for CORS/RTK recording).
  std::function<void(const prism::RoverRtcmChunkView&)> on_rover_rtcm;

  // Heartbeat (RK system time).
  std::function<void(const prism::HeartbeatStatus&)> on_heartbeat;

  // Device info update.
  std::function<void(const prism::DeviceInfo&, communication::TimeSyncProvider)>
      on_device_info;

  // GNSS timing status.
  std::function<void(const prism::GnssTimingStatus&)> on_gnss_timing;

  // LiDAR status.
  std::function<void(const prism::LidarStatus&)> on_lidar_status;

  // Log message.
  std::function<void(const std::string&)> on_log;

  // Post a callable to the Qt main thread. The consumer sets this.
  std::function<void(std::function<void()>)> post;

  // Whether the source can record. False for replay-only sources.
  virtual bool can_record() const { return true; }

  // Whether the source supports runtime camera exposure control.
  virtual bool can_control_exposure() const { return false; }

  // Whether the source supports Wi-Fi hotspot control.
  virtual bool can_control_wifi() const { return false; }

  // Whether the source supports device time synchronization.
  virtual bool can_sync_time() const { return false; }

  // Query the current device info (for sources that have one).
  virtual std::optional<prism::DeviceInfo> device_info() const { return {}; }

  // Query the current time sync provider.
  virtual communication::TimeSyncProvider time_sync_provider() const {
    return communication::TimeSyncProvider::Unsynced;
  }
};

}  // namespace prism_viewer::stream
