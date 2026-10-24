#pragma once

#include "prism/usb/runtime_api.hpp"
#include "prism/usb/camera_control.hpp"

#include <functional>
#include <stdexcept>
#include <utility>

namespace prism_runtime {

#ifndef _WIN32

using Client = prism::Client;
using ImuStream = prism::ImuStream;
using LidarStream = prism::LidarStream;
using RoverRtcmStream = prism::RoverRtcmStream;

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
inline prism::ImuSample parseImuSample(const prism::Frame& frame) {
  return prism::parseImuSample(frame);
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
  void openNetworkDevice(const prism::NetworkOptions& options);
  bool isNetwork() const;
  uint32_t networkCameraMaxFps() const;
  void closeDevice();
  bool isOpen() const;
  std::wstring path() const;
  std::wstring serialNumber() const;
  bool keepaliveEnabled() const;
  bool streamTransferActive() const noexcept;

  prism::HelloInfo hello();
  std::vector<prism::RecordedDataset> recordedDatasets();
  std::string downloadRecordedDataset(const std::string& name,const std::string& parent,
      const prism::DatasetProgress& progress={},const prism::DatasetCancel& cancel={});
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
  prism::ExposureLimits cameraExposureLimits();
  prism::ExposureLimits setCameraExposureLimits(
      const prism::ExposureLimits& limits,
      uint32_t field_mask = prism::kExposureLimitsFieldAll);
  prism::GnssTimingStatus gnssTimingStatus();
  prism::TimeSyncPortStatus timeSyncPortStatus();
  prism::TimeSyncPortStatus setTimeSyncPortMode(prism::TimeSyncPortMode mode);
  prism::TimeSyncCorsStatus saveTimeSyncCorsConfiguration(const prism::TimeSyncCorsConfiguration& configuration);
  prism::TimeSyncRtkStatus startRtk(const prism::RtkStartOptions& options);
  prism::TimeSyncRtkStatus stopRtk(uint32_t timeout_ms = 5000);
  prism::TimeSyncRtkStatus timeSyncRtkStatus();
  prism::TimeSyncRtkVersions timeSyncRtkVersions();
  prism::GnssReceptionStatus gnssReceptionStatus();
  prism::GnssObservations gnssObservations(uint64_t cursor=0, uint64_t session=0);
  prism::GnssRawBatch gnssRaw(uint64_t cursor=0, uint64_t session=0);
  prism::RtkCorrectionStatus beginRtkCorrections();
  prism::RtkCorrectionStatus sendRtkCorrections(
      const uint8_t* data, size_t size, uint32_t timeout_ms = 3000);
  prism::RtkCorrectionStatus endRtkCorrections();
  prism::RtkCorrectionStatus rtkCorrectionStatus();
  prism::RoverRtcmStatus startRoverRtcm();
  prism::RoverRtcmStatus stopRoverRtcm();
  prism::VideoStatus startVideo1280x1024(uint32_t fps = 0);
  prism::VideoStatus startPrism415mVideo(uint32_t fps = 2);
  prism::VideoStatus startProfileVideoGpu(uint32_t fps = 0);
  uint32_t beginLiveExposureRows(uint8_t mask, const std::array<uint16_t,4>& rows);
  bool finishLiveExposureRows(const prism::Frame&, prism::ExposureConfiguration& applied);
  bool liveExposureRowsPending() const;
  prism::CameraControlIdleStatus cameraControlIdle();
  prism::CameraControlIdleStatus stageCameraControlIdle(const prism::CameraControlIdleStatus&,
      const std::array<prism::CameraRowGainConfig,4>&);
  uint32_t beginCameraControlGet();
  uint32_t beginCameraControlSet(const prism::CameraControlStatus&,
      const std::array<prism::CameraRowGainConfig,4>&);
  bool finishCameraControl(const prism::Frame&, prism::CameraControlStatus&);
  bool cameraControlPending() const;
  void stopVideo();
  void sendVideoAck(uint32_t last_frame_id);
  prism::LidarStatus lidarStatus();
  prism::LidarPowerStatus lidarPowerStatus(prism::LidarModel model, uint32_t timeout_ms=3000);
  prism::LidarPowerStatus setLidarStandby(prism::LidarModel model, bool standby, uint32_t timeout_ms=15000);
  prism::LidarSpeedStatus lidarSpeedStatus(prism::LidarModel model,uint32_t timeout_ms=3000);
  prism::LidarSpeedStatus setLidarSpeedMode(prism::LidarModel model,prism::LidarSpeedMode mode,uint32_t timeout_ms=5000);
  prism::LidarNetworkStatus lidarNetworkStatus();
  prism::LidarNetworkStatus saveLidarNetworkConfiguration(
      const prism::LidarNetworkConfiguration& configuration);
  prism::LidarNetworkStatus probeLidarNetwork();
  prism::Frame readFrame(uint32_t timeout_ms = 3000);
  prism::SystemUpgradeResult upgradeSystem(
      const std::string& package_path,
      const prism::UpgradeOptions& options = {},
      const std::function<void(const prism::SystemUpgradeProgress&)>& progress = {});

 private:
  friend class ImuStream;
  friend class LidarStream;
  friend class RoverRtcmStream;
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
  LidarStream(Client& client, prism::LidarPointBatchHandler point_handler,
              prism::LidarImuSampleHandler imu_handler = {});
  ~LidarStream();
  void start(prism::LidarModel model);
  void stop();
  bool active() const noexcept;
  bool handleFrame(const prism::Frame& frame);

 private:
  Client* client_;
  prism::LidarPointBatchHandler point_handler_;
  prism::LidarImuSampleHandler imu_handler_;
  bool active_ = false;
};

class RoverRtcmStream {
 public:
  RoverRtcmStream(Client& client, prism::RoverRtcmHandler handler);
  ~RoverRtcmStream();
  void start();
  void stop();
  bool active() const noexcept;
  bool handleFrame(const prism::Frame& frame);

 private:
  Client* client_;
  prism::RoverRtcmHandler handler_;
  bool active_ = false;
};

prism::SystemUpgradePackageInfo inspectSystemUpgradePackage(
    const std::string& path);
prism::HeartbeatStatus parseHeartbeat(const prism::Frame& frame);
prism::VideoChunkView parseVideoChunkView(const prism::Frame& frame);
prism::VideoMeta parseVideoMeta(const prism::Frame& frame);
prism::ImuSample parseImuSample(const prism::Frame& frame);
const char* usbLinkSpeedName(prism::UsbLinkSpeed speed);
const char* sensorBoardErrorCodeName(prism::SensorBoardErrorCode code);

#endif

// Borrow samples from an already-started aggregate camera+IMU stream.
// No Client/control methods: cannot issue a second START/STOP.
class AggregateImuObserver {
 public:
  explicit AggregateImuObserver(prism::ImuSampleHandler handler)
      : handler_(std::move(handler)) {
    if (!handler_) throw std::invalid_argument("aggregate IMU handler required");
  }
  bool handleFrame(const prism::Frame& frame) const {
    if (frame.type != prism::FrameType::ImuSample) return false;
    handler_(prism_runtime::parseImuSample(frame));
    return true;
  }
 private:
  prism::ImuSampleHandler handler_;
};

}  // namespace prism_runtime
