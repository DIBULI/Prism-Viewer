#pragma once

#include "prism/usb/runtime_api.hpp"

#include <functional>

namespace prism_runtime {

#ifndef _WIN32

using Client = prism::Client;
using ImuStream = prism::ImuStream;
using LidarStream = prism::LidarStream;

inline prism::SystemUpgradePackageInfo inspectSystemUpgradePackage(
    const std::string& path) {
  return prism::inspectSystemUpgradePackage(path);
}
inline prism::HeartbeatStatus parseHeartbeat(const prism::Frame& frame) {
  return prism::parseHeartbeat(frame);
}
inline prism::VideoChunkView parseVideoChunkView(const prism::Frame& frame) {
  return prism::parseVideoChunkView(frame);
}
inline prism::VideoMeta parseVideoMeta(const prism::Frame& frame) {
  return prism::parseVideoMeta(frame);
}
inline const char* usbLinkSpeedName(prism::UsbLinkSpeed speed) {
  return prism::usbLinkSpeedName(speed);
}
inline const char* sensorBoardErrorCodeName(
    prism::SensorBoardErrorCode code) {
  return prism::sensorBoardErrorCodeName(code);
}

#else

class Client {
 public:
  Client();
  ~Client();

  Client(const Client&) = delete;
  Client& operator=(const Client&) = delete;

  static std::vector<prism::DeviceInfo> enumerate(
      uint16_t vid = prism::kDefaultVid, uint16_t pid = prism::kDefaultPid);
  void openDevice(const prism::DeviceInfo& device);
  void closeDevice();
  bool isOpen() const;
  std::wstring path() const;
  std::wstring serialNumber() const;
  bool keepaliveEnabled() const;
  bool streamTransferActive() const noexcept;

  prism::HelloInfo hello();
  prism::DeviceInfo deviceInfo();
  prism::DeviceVersions deviceVersions();
  prism::SystemTimeSyncResult synchronizeSystemTime(
      uint32_t sample_count = 12,
      uint32_t verification_sample_count = 6,
      uint32_t timeout_ms = 1000);
  prism::NetworkInfo networkInfo();
  prism::WifiHotspotStatus wifiHotspotStatus();
  prism::WifiHotspotStatus setWifiHotspotEnabled(bool enabled);
  prism::DeviceConfiguration deviceConfiguration();
  prism::DeviceConfiguration saveDeviceConfiguration(
      const prism::DeviceConfiguration& configuration,
      uint32_t field_mask = prism::kDeviceConfigFieldAll);
  prism::ExposureConfiguration cameraExposure();
  prism::ExposureConfiguration setExposureConfiguration(
      const prism::ExposureConfiguration& configuration,
      uint32_t field_mask = prism::kExposureFieldAll);
  prism::VideoStatus startVideo1280x1024(uint32_t fps = 0);
  void stopVideo();
  void sendVideoAck(uint32_t last_frame_id);
  prism::LidarStatus lidarStatus();
  prism::Frame readFrame(uint32_t timeout_ms = 3000);
  prism::SystemUpgradeResult upgradeSystem(
      const std::string& package_path,
      const prism::UpgradeOptions& options = {},
      const std::function<void(const prism::SystemUpgradeProgress&)>& progress = {});

 private:
  friend class ImuStream;
  friend class LidarStream;
  const prism::RuntimeApi* api_ = nullptr;
  prism::Client* handle_ = nullptr;
};

class ImuStream {
 public:
  ImuStream(Client& client, prism::ImuSampleHandler handler);
  ~ImuStream();
  void start(uint32_t sensor_count = 2, uint32_t nominal_rate_hz = 0);
  void stop();
  bool active() const noexcept;
  bool handleFrame(const prism::Frame& frame);

 private:
  Client* client_;
  prism::ImuSampleHandler handler_;
  bool active_ = false;
};

class LidarStream {
 public:
  LidarStream(Client& client, prism::LidarPointBatchHandler handler);
  ~LidarStream();
  void start(prism::LidarModel model);
  void stop();
  bool active() const noexcept;
  bool handleFrame(const prism::Frame& frame);

 private:
  Client* client_;
  prism::LidarPointBatchHandler handler_;
  bool active_ = false;
};

prism::SystemUpgradePackageInfo inspectSystemUpgradePackage(
    const std::string& path);
prism::HeartbeatStatus parseHeartbeat(const prism::Frame& frame);
prism::VideoChunkView parseVideoChunkView(const prism::Frame& frame);
prism::VideoMeta parseVideoMeta(const prism::Frame& frame);
const char* usbLinkSpeedName(prism::UsbLinkSpeed speed);
const char* sensorBoardErrorCodeName(prism::SensorBoardErrorCode code);

#endif

}  // namespace prism_runtime
