#pragma once

#include <cstddef>
#include <cstdint>

#include "prism/usb/client.hpp"
#include "prism/usb/streams.hpp"

namespace prism {

constexpr uint32_t kRuntimeApiVersion = 4;
inline constexpr char kRuntimeApiEntryPoint[] =
    "prism_usb_sdk_get_runtime_api";

// Versioned MSVC C++ ABI table used by Windows applications that load the SDK
// with LoadLibrary/GetProcAddress instead of linking its import library. Both
// sides must use the compatible MSVC 14.x runtime and the same SDK headers.
struct RuntimeApi {
  uint32_t abi_version;
  uint32_t struct_size;
  uint32_t msvc_version;
  const char* sdk_version;

  Client* (*client_create)();
  void (*client_destroy)(Client*) noexcept;
  std::vector<DeviceInfo> (*enumerate)(uint16_t, uint16_t);
  void (*open_device)(Client*, const DeviceInfo&);
  void (*close_device)(Client*);
  bool (*is_open)(const Client*);
  std::wstring (*path)(const Client*);
  std::wstring (*serial_number)(const Client*);
  bool (*keepalive_enabled)(const Client*);
  bool (*stream_transfer_active)(const Client*) noexcept;

  HelloInfo (*hello)(Client*);
  DeviceInfo (*device_info)(Client*);
  DeviceVersions (*device_versions)(Client*);
  SystemTimeSyncResult (*synchronize_system_time)(Client*, uint32_t, uint32_t,
                                                   uint32_t);
  NetworkInfo (*network_info)(Client*);
  WifiHotspotStatus (*wifi_hotspot_status)(Client*);
  WifiHotspotStatus (*set_wifi_hotspot_enabled)(Client*, bool);
  DeviceConfiguration (*device_configuration)(Client*);
  DeviceConfiguration (*save_device_configuration)(
      Client*, const DeviceConfiguration&, uint32_t);
  ExposureConfiguration (*camera_exposure)(Client*);
  ExposureConfiguration (*set_exposure_configuration)(
      Client*, const ExposureConfiguration&, uint32_t);

  VideoStatus (*start_video)(Client*, uint32_t);
  void (*stop_video)(Client*);
  ImuStreamStatus (*start_imu)(Client*, uint32_t, uint32_t);
  ImuStreamStatus (*stop_imu)(Client*);
  void (*send_video_ack)(Client*, uint32_t);
  LidarStatus (*start_lidar)(Client*, LidarModel);
  LidarStatus (*stop_lidar)(Client*);
  LidarStatus (*lidar_status)(Client*);
  LidarNetworkStatus (*lidar_network_status)(Client*);
  LidarNetworkStatus (*save_lidar_network_configuration)(
      Client*, const LidarNetworkConfiguration&);
  LidarNetworkStatus (*probe_lidar_network)(Client*);
  Frame (*read_frame)(Client*, uint32_t);

  SystemUpgradePackageInfo (*inspect_system_upgrade_package)(
      const std::string&);
  SystemUpgradeResult (*upgrade_system)(
      Client*, const std::string&, const UpgradeOptions&,
      const std::function<void(const SystemUpgradeProgress&)>&);

  HeartbeatStatus (*parse_heartbeat)(const Frame&);
  VideoChunkView (*parse_video_chunk_view)(const Frame&);
  VideoMeta (*parse_video_meta)(const Frame&);
  ImuSample (*parse_imu_sample)(const Frame&);
  LidarPointBatch (*parse_lidar_point_batch)(const Frame&);
  const char* (*usb_link_speed_name)(UsbLinkSpeed);
  const char* (*sensor_board_error_code_name)(SensorBoardErrorCode);
  LidarImuSample (*parse_lidar_imu_sample)(const Frame&);
};

using GetRuntimeApiFunction = const RuntimeApi* (*)(uint32_t);

}  // namespace prism
