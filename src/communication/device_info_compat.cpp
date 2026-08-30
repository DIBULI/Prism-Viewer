#include "communication/device_info_compat.hpp"

#include "prism/usb/configuration.hpp"
#include "prism/usb/device_info.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace prism_viewer::communication {
namespace {

constexpr uint16_t kLegacyDeviceInfoVersion = 3u;
constexpr uint16_t kTimeSourceDeviceInfoVersion = 4u;
constexpr size_t kTimeSyncSourceOffset = 254u;
constexpr uint32_t kUsb3Connected = 1u << 0u;
constexpr uint32_t kSensorBoardOnline = 1u << 1u;
constexpr uint32_t kSensorBoardTimeSynced = 1u << 2u;
constexpr uint32_t kWifiPresent = 1u << 6u;
constexpr uint32_t kWifiEnabled = 1u << 7u;
constexpr uint32_t kWifiApRunning = 1u << 8u;
constexpr uint32_t kWifiDhcpRunning = 1u << 9u;
constexpr uint32_t kWifiPersisted = 1u << 10u;
constexpr uint32_t kKnownFlags = (1u << 11u) - 1u;

uint16_t readLe16(const std::vector<uint8_t>& bytes, size_t offset) {
  if (offset + 2u > bytes.size()) {
    throw std::runtime_error("truncated DeviceInfo response");
  }
  return static_cast<uint16_t>(bytes[offset]) |
         (static_cast<uint16_t>(bytes[offset + 1u]) << 8u);
}

uint32_t readLe32(const std::vector<uint8_t>& bytes, size_t offset) {
  if (offset + 4u > bytes.size()) {
    throw std::runtime_error("truncated DeviceInfo response");
  }
  uint32_t value = 0u;
  for (size_t index = 0; index < 4u; ++index) {
    value |= static_cast<uint32_t>(bytes[offset + index])
             << (index * 8u);
  }
  return value;
}

uint8_t popcount8(uint8_t value) {
  uint8_t count = 0u;
  while (value != 0u) {
    count = static_cast<uint8_t>(count + (value & 1u));
    value = static_cast<uint8_t>(value >> 1u);
  }
  return count;
}

std::string fixedString(const std::vector<uint8_t>& bytes, size_t offset,
                        size_t size) {
  if (offset + size > bytes.size()) {
    throw std::runtime_error("truncated DeviceInfo string field");
  }
  const auto begin = bytes.begin() + static_cast<std::ptrdiff_t>(offset);
  const auto end = begin + static_cast<std::ptrdiff_t>(size);
  const auto terminator = std::find(begin, end, 0u);
  return std::string(begin, terminator);
}

}  // namespace

DeviceInfoStatus parseCompatibleDeviceInfo(const prism::Frame& frame) {
  if (frame.type != prism::FrameType::DeviceInfoResponse ||
      frame.payload.size() != prism::kDeviceInfoPayloadSize) {
    throw std::runtime_error("not a DeviceInfo response");
  }

  const uint16_t version = readLe16(frame.payload, 0u);
  if (version == kLegacyDeviceInfoVersion) {
    DeviceInfoStatus status;
    status.info = prism::parseDeviceInfo(frame);
    status.time_sync_provider =
        status.info.sensor_board_time_synced
            ? TimeSyncProvider::LegacyUnknown
            : TimeSyncProvider::Unsynced;
    return status;
  }
  if (version != kTimeSourceDeviceInfoVersion) {
    throw std::runtime_error("unsupported DeviceInfo payload");
  }

  const auto& payload = frame.payload;
  const uint32_t flags = readLe32(payload, 4u);
  if ((flags & ~kKnownFlags) != 0u || payload[255] != 0u) {
    throw std::runtime_error(
        "invalid DeviceInfo flags or reserved bytes");
  }
  const uint8_t provider_value = payload[kTimeSyncSourceOffset];
  if (provider_value > static_cast<uint8_t>(TimeSyncProvider::Gps)) {
    throw std::runtime_error("invalid sensor-board time sync provider");
  }
  const uint8_t error_code_value = payload[19];
  if (error_code_value >
      static_cast<uint8_t>(prism::SensorBoardErrorCode::Multiple)) {
    throw std::runtime_error("invalid sensor-board error code");
  }
  const uint32_t error_flags = readLe32(payload, 28u);
  if ((error_flags &
       ~static_cast<uint32_t>(prism::SensorBoardKnownErrorMask)) != 0u ||
      ((error_flags == 0u) !=
       (error_code_value ==
        static_cast<uint8_t>(prism::SensorBoardErrorCode::None)))) {
    throw std::runtime_error("inconsistent sensor-board error status");
  }
  const uint8_t speed_value = payload[8];
  if (speed_value >
      static_cast<uint8_t>(prism::UsbLinkSpeed::UsbSuperSpeedPlus)) {
    throw std::runtime_error("invalid DeviceInfo USB speed");
  }

  DeviceInfoStatus status;
  auto& info = status.info;
  info.info_version = version;
  info.usb_speed = static_cast<prism::UsbLinkSpeed>(speed_value);
  info.usb3_connected = (flags & kUsb3Connected) != 0u;
  info.sensor_board_online = (flags & kSensorBoardOnline) != 0u;
  info.sensor_board_time_synced =
      (flags & kSensorBoardTimeSynced) != 0u;
  info.detected_imu_count = payload[9];
  info.detected_camera_count = payload[10];
  info.imu_present_mask = payload[11];
  info.imu_receiving_mask = payload[12];
  info.imu_time_synced_mask = payload[13];
  info.imu_init_error_mask = payload[14];
  info.camera_present_mask = payload[15];
  info.camera_streaming_mask = payload[16];
  info.sensor_board_error_code =
      static_cast<prism::SensorBoardErrorCode>(error_code_value);
  info.sensor_board_error_flags = error_flags;
  for (size_t index = 0; index < info.imu_init_error_reason.size();
       ++index) {
    const uint8_t reason = payload[17u + index];
    if (reason >
        static_cast<uint8_t>(prism::ImuInitErrorReason::Unknown)) {
      throw std::runtime_error(
          "invalid IMU initialization error reason");
    }
    info.imu_init_error_reason[index] =
        static_cast<prism::ImuInitErrorReason>(reason);
  }
  info.imu_fps = readLe16(payload, 20u);
  info.camera_fps = readLe16(payload, 22u);
  info.wifi.error_code =
      static_cast<int32_t>(readLe32(payload, 24u));
  info.product_serial = fixedString(payload, 32u, 33u);
  info.wifi.present = (flags & kWifiPresent) != 0u;
  info.wifi.enabled = (flags & kWifiEnabled) != 0u;
  info.wifi.access_point_running = (flags & kWifiApRunning) != 0u;
  info.wifi.dhcp_running = (flags & kWifiDhcpRunning) != 0u;
  info.wifi.persisted = (flags & kWifiPersisted) != 0u;
  info.wifi.interface_name = fixedString(payload, 65u, 16u);
  info.wifi.ssid = fixedString(payload, 81u, 33u);
  info.wifi.address = fixedString(payload, 114u, 16u);
  info.sensor_board_error = fixedString(payload, 130u, 64u);
  info.wifi.error = fixedString(payload, 194u, 60u);
  status.time_sync_provider =
      static_cast<TimeSyncProvider>(provider_value);

  const bool provider_is_synced =
      status.time_sync_provider != TimeSyncProvider::Unsynced;
  if ((info.imu_present_mask & ~0x03u) != 0u ||
      (info.imu_receiving_mask & ~0x03u) != 0u ||
      (info.imu_time_synced_mask & ~0x03u) != 0u ||
      (info.imu_init_error_mask & ~0x03u) != 0u ||
      (info.camera_present_mask & ~0x0fu) != 0u ||
      (info.camera_streaming_mask & ~0x0fu) != 0u ||
      info.detected_imu_count != popcount8(info.imu_present_mask) ||
      info.detected_camera_count != popcount8(info.camera_present_mask) ||
      (info.usb3_connected !=
       (info.usb_speed >= prism::UsbLinkSpeed::UsbSuperSpeed)) ||
      info.sensor_board_time_synced != provider_is_synced ||
      (info.imu_fps != 500u && info.imu_fps != 1000u) ||
      !prism::isCameraFpsSupported(info.camera_fps)) {
    throw std::runtime_error("inconsistent DeviceInfo payload");
  }
  for (size_t index = 0; index < info.imu_init_error_reason.size();
       ++index) {
    const bool has_error =
        (info.imu_init_error_mask &
         static_cast<uint8_t>(1u << index)) != 0u;
    const bool has_reason =
        info.imu_init_error_reason[index] !=
        prism::ImuInitErrorReason::None;
    if (has_error != has_reason) {
      throw std::runtime_error(
          "inconsistent IMU initialization error reason");
    }
  }
  return status;
}

DeviceInfoStatus readDeviceInfo(prism_runtime::Client& client) {
#ifdef _WIN32
  DeviceInfoStatus status;
  status.info = client.deviceInfo();
  status.time_sync_provider =
      status.info.sensor_board_time_synced
          ? TimeSyncProvider::LegacyUnknown
          : TimeSyncProvider::Unsynced;
  return status;
#else
  DeviceInfoStatus status = parseCompatibleDeviceInfo(
      client.command(prism::FrameType::DeviceInfo, {}, 3000u));
  status.info.path = client.path();
  status.info.serial_number = client.serialNumber();
  return status;
#endif
}

const char* timeSyncProviderName(TimeSyncProvider provider) {
  switch (provider) {
    case TimeSyncProvider::Unsynced: return "none";
    case TimeSyncProvider::RkPtp: return "RK (PTP)";
    case TimeSyncProvider::Gps: return "GPS";
    case TimeSyncProvider::LegacyUnknown: return "unknown (legacy DeviceInfo)";
  }
  return "unknown";
}

}  // namespace prism_viewer::communication
