#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace prism {

constexpr uint16_t kDefaultVid = 0x2207;
constexpr uint16_t kDefaultPid = 0x1201;
constexpr uint8_t kProtocolVersion = 10;
constexpr uint32_t kDefaultKeepaliveIntervalMs = 1000;

enum class AgentUpdateResult : uint16_t {
  None = 0,
  Pending = 1,
  Success = 2,
  Rollback = 3,
  Failed = 4,
};

enum class FrameType : uint8_t {
  Hello = 0x01,
  Time = 0x02,
  Ping = 0x03,
  DeviceInfo = 0x04,
  VideoStart = 0x06,
  VideoStop = 0x07,
  ImuStart = 0x08,
  ImuStop = 0x09,
  StreamAck = 0x0a,
  NetworkInfo = 0x0b,
  Keepalive = 0x0c,
  TimeSync = 0x0d,
  TimeSet = 0x0e,
  WifiHotspotGet = 0x0f,
  WifiHotspotSet = 0x10,
  LidarStart = 0x11,
  LidarStop = 0x12,
  LidarStatus = 0x13,
  LidarNetworkGet = 0x14,
  LidarNetworkSet = 0x15,
  LidarNetworkProbe = 0x16,
  UpgradeBegin = 0x20,
  UpgradeChunk = 0x21,
  UpgradeCommit = 0x22,
  UpgradeAbort = 0x23,
  SensorBoardUpgradeBegin = 0x24,
  SensorBoardUpgradeChunk = 0x25,
  SensorBoardUpgradeCommit = 0x26,
  SensorBoardUpgradeAbort = 0x27,
  ConfigGet = 0x32,
  ConfigSet = 0x33,
  ExposureGet = 0x34,
  ExposureSet = 0x35,
  HelloResponse = 0x81,
  TimeResponse = 0x82,
  Pong = 0x83,
  Heartbeat = 0x84,
  DeviceInfoResponse = 0x85,
  VideoStatus = 0x88,
  ImuSample = 0x89,
  ImuStatus = 0x8a,
  VideoChunk = 0x8b,
  VideoMeta = 0x8c,
  NetworkInfoResponse = 0x8d,
  TimeSyncResponse = 0x8e,
  TimeSetResponse = 0x8f,
  WifiHotspotStatus = 0x90,
  LidarStatusResponse = 0x91,
  LidarPoints = 0x92,
  LidarNetworkStatus = 0x93,
  LidarImuSample = 0x94,
  UpgradeStatus = 0xa0,
  SensorBoardUpgradeStatus = 0xa1,
  ConfigResponse = 0xb2,
  ExposureResponse = 0xb3,
  Error = 0xff,
};

enum class LidarModel : uint8_t {
  None = 0,
  Mid360 = 1,
  Mid360S = 2,
};

enum class UsbLinkSpeed : uint8_t {
  Unknown = 0,
  UsbLowSpeed = 1,
  UsbFullSpeed = 2,
  UsbHighSpeed = 3,
  UsbSuperSpeed = 4,
  UsbSuperSpeedPlus = 5,
};

enum class ImuInitErrorReason : uint8_t {
  None = 0,
  WhoAmIMismatch = 1,
  ConfigurationReadbackMismatch = 2,
  FifoBusInvalid = 3,
  SampleTimeout = 4,
  Unknown = 5,
};

enum class SensorBoardErrorCode : uint8_t {
  None = 0,
  CameraInitialization = 1,
  TcInitialization = 2,
  CameraRuntime = 3,
  DdrFifoOverflow = 4,
  DdrAxi = 5,
  TcUnderflow = 6,
  TcClockLost = 7,
  CameraStreamStalled = 8,
  CameraNotReady = 9,
  ControlLinkOffline = 10,
  Multiple = 11,
};

enum SensorBoardErrorFlag : uint32_t {
  SensorBoardCameraInitMask = 0x0000000fu,
  SensorBoardTcInit = 0x00000010u,
  SensorBoardCameraRuntimeMask = 0x00000f00u,
  SensorBoardDdrFifoOverflow = 0x00001000u,
  SensorBoardDdrAxi = 0x00002000u,
  SensorBoardTcUnderflow = 0x00004000u,
  SensorBoardTcClockLost = 0x00008000u,
  SensorBoardCameraStreamStalled = 0x01000000u,
  SensorBoardCameraNotReady = 0x02000000u,
  SensorBoardControlLinkOffline = 0x04000000u,
  SensorBoardKnownErrorMask = 0x0700ff1fu,
};

struct DeviceWifiInfo {
  bool present = false;
  bool enabled = false;
  bool access_point_running = false;
  bool dhcp_running = false;
  bool persisted = false;
  int32_t error_code = 0;
  std::string interface_name;
  std::string ssid;
  std::string address;
  std::string error;
};

struct DeviceInfo {
  std::wstring path;
  std::wstring serial_number;
  uint16_t vendor_id = kDefaultVid;
  uint16_t product_id = kDefaultPid;

  // Status fields are filled by Client::deviceInfo(). Enumeration leaves
  // them at their default values.
  uint16_t info_version = 0;
  UsbLinkSpeed usb_speed = UsbLinkSpeed::Unknown;
  bool usb3_connected = false;
  bool sensor_board_online = false;
  bool sensor_board_time_synced = false;
  uint8_t detected_imu_count = 0;
  uint8_t detected_camera_count = 0;
  uint8_t imu_present_mask = 0;
  uint8_t imu_receiving_mask = 0;
  uint8_t imu_time_synced_mask = 0;
  uint8_t imu_init_error_mask = 0;
  std::array<ImuInitErrorReason, 2> imu_init_error_reason{
      ImuInitErrorReason::None, ImuInitErrorReason::None};
  uint8_t camera_present_mask = 0;
  uint8_t camera_streaming_mask = 0;
  SensorBoardErrorCode sensor_board_error_code =
      SensorBoardErrorCode::None;
  uint32_t sensor_board_error_flags = 0;
  std::string sensor_board_error;
  uint16_t imu_fps = 0;
  uint16_t camera_fps = 0;
  std::string product_serial;
  DeviceWifiInfo wifi;
};

struct Frame {
  FrameType type = FrameType::Error;
  uint16_t flags = 0;
  uint32_t sequence = 0;
  std::vector<uint8_t> payload;
};

struct HelloInfo {
  uint16_t protocol_version = 0;
  uint16_t header_size = 0;
  uint32_t max_payload = 0;
  std::string app;
  std::string version;
  uint32_t process_id = 0;
  uint64_t process_started_monotonic_us = 0;
  AgentUpdateResult update_result = AgentUpdateResult::None;
  std::string update_version;
  std::string sensor_board_version;
};

// Semantic version embedded in this Host SDK library. The agent must report
// exactly the same version before Client::open/openFirst succeeds.
std::string hostSdkVersion();

struct DeviceVersions {
  std::string agent;
  std::string sensor_board;
  std::string combined;
};

std::string frameTypeName(FrameType type);

}  // namespace prism
