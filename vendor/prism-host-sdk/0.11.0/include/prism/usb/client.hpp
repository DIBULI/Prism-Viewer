#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "prism/usb/common.hpp"
#include "prism/usb/configuration.hpp"
#include "prism/usb/device_info.hpp"
#include "prism/usb/exposure.hpp"
#include "prism/usb/telemetry.hpp"
#include "prism/usb/time_sync.hpp"
#include "prism/usb/update.hpp"
#include "prism/usb/wifi.hpp"

namespace prism {

class Client {
 public:
  Client();
  ~Client();

  Client(const Client&) = delete;
  Client& operator=(const Client&) = delete;
  Client(Client&&) noexcept;
  Client& operator=(Client&&) noexcept;

  static std::vector<DeviceInfo> enumerate(uint16_t vid = kDefaultVid,
                                           uint16_t pid = kDefaultPid);
  static Client openFirst(uint16_t vid = kDefaultVid, uint16_t pid = kDefaultPid);
  static Client open(const DeviceInfo& device);

  // Explicit device lifecycle for applications that keep one Client object.
  // The static factories provide the same strict version-checked lifecycle.
  void openFirstDevice(uint16_t vid = kDefaultVid, uint16_t pid = kDefaultPid);
  void openDevice(const DeviceInfo& device);
  void closeDevice();

  bool isOpen() const;
  std::wstring path() const;
  std::wstring serialNumber() const;
  void close();
  void setKeepaliveEnabled(
      bool enabled, uint32_t interval_ms = kDefaultKeepaliveIntervalMs);
  bool keepaliveEnabled() const;

  HelloInfo hello();
  // Returns a fresh device status snapshot. Unlike heartbeat, this contains
  // device status and can be queried while camera/IMU streams are active.
  DeviceInfo deviceInfo();
  DeviceVersions deviceVersions();
  TimeInfo boardTime();
  // Estimates the device-minus-host wall-clock offset using the four NTP
  // timestamps. This never changes either system clock. It is intentionally
  // rejected while a video, IMU, or LiDAR transfer is active.
  NtpTimeSyncResult synchronizeTimeNtpLike(
      uint32_t sample_count = 12, uint32_t timeout_ms = 1000);
  // Makes the host wall clock authoritative for RK CLOCK_REALTIME, writes the
  // corrected UTC value to the RK hardware clock, then remeasures the offset.
  // The operation is rejected while video, IMU, or LiDAR transfer is active.
  SystemTimeSyncResult synchronizeSystemTime(
      uint32_t sample_count = 12,
      uint32_t verification_sample_count = 6,
      uint32_t timeout_ms = 1000);
  bool streamTransferActive() const noexcept;
  uint64_t ping();
  NetworkInfo networkInfo();
  // WiFi hotspot control shares the USB receive endpoint with live streams.
  // Both calls are therefore rejected while any streaming transfer is active.
  WifiHotspotStatus wifiHotspotStatus();
  WifiHotspotStatus setWifiHotspotEnabled(bool enabled);

  // Persistent device configuration. Rate writes are idle-only and are
  // atomically stored by the agent under /var/lib/prism.
  DeviceConfiguration deviceConfiguration();
  DeviceConfiguration saveDeviceConfiguration(
      const DeviceConfiguration& configuration,
      uint32_t field_mask = kDeviceConfigFieldAll);

  // Runtime-only exposure control. These values are not persisted and may be
  // read or changed while acquisition is active. Automatic exposure uses one
  // target brightness shared by all four cameras; manual exposure time is
  // independent for each camera.
  ExposureConfiguration cameraExposure();
  ExposureConfiguration setExposureConfiguration(
      const ExposureConfiguration& configuration,
      uint32_t field_mask = kExposureFieldAll);
  ExposureConfiguration setAutoExposureTargetBrightness(
      uint8_t target_brightness);
  ExposureConfiguration setCameraExposure(
      uint8_t camera_index,
      const CameraExposureConfiguration& exposure);

  // Camera and IMU share one aggregate capture session. Video start enables
  // both paths; startImu confirms the IMU selection/rate for that session.
  VideoStatus startVideo1280x1024(uint32_t fps = 0);
  // Either stop method stops both Camera and IMU acquisition.
  void stopVideo();
  ImuStreamStatus startImu(uint32_t sensor_count = 2,
                           uint32_t nominal_rate_hz = 0);
  ImuStreamStatus stopImu();
  void sendVideoAck(uint32_t last_frame_id);

  // The model is intentionally mandatory: Mid-360 and Mid-360S use separate
  // SDK2 configuration schemas and the Agent never guesses from discovery.
  LidarStatus startLidar(LidarModel model);
  LidarStatus stopLidar();
  LidarStatus lidarStatus();
  LidarNetworkStatus lidarNetworkStatus();
  LidarNetworkStatus saveLidarNetworkConfiguration(
      const LidarNetworkConfiguration& configuration);
  LidarNetworkStatus probeLidarNetwork();

  Frame readFrame(uint32_t timeout_ms = 3000);
  Frame command(FrameType type, const std::vector<uint8_t>& payload = {},
                uint32_t timeout_ms = 3000);

  // System upgrades are package-only. A valid ZIP always contains both the
  // RK agent and sensor-board BOOT.BIN; standalone sensor-board OTA is not
  // exposed by the public SDK.
  SystemUpgradeResult upgradeSystem(
      const std::string& package_path,
      const UpgradeOptions& options = {},
      const std::function<void(const SystemUpgradeProgress&)>& progress = {});

 private:
  UpgradeStatus upgradeAgentImage(
      const std::vector<uint8_t>& image,
      const UpgradeOptions& options,
      const std::function<void(const UpgradeStatus&)>& progress);
  SensorBoardUpgradeStatus upgradeSensorBoardImage(
      const std::vector<uint8_t>& image,
      const UpgradeOptions& options,
      const std::function<void(const SensorBoardUpgradeStatus&)>& progress);
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace prism
