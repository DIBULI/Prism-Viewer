#pragma once

#include "communication/prism_runtime.hpp"

#include <cstdint>

namespace prism_viewer::communication {

enum class TimeSyncProvider : uint8_t {
  SensorBoardInternal = 0,
  Unsynced = SensorBoardInternal,
  RkPtp = 1,
  Gps = 2,
  LegacyUnknown = 0xff,
};

struct DeviceInfoStatus {
  prism::DeviceInfo info;
  TimeSyncProvider time_sync_provider = TimeSyncProvider::SensorBoardInternal;
};

// Parses both the public Host SDK 1.0 DeviceInfo v3 response and Agent 1.1
// DeviceInfo v4, which adds the authoritative sensor-board time source.
DeviceInfoStatus parseCompatibleDeviceInfo(const prism::Frame& frame);
DeviceInfoStatus readDeviceInfo(prism_runtime::Client& client);

const char* timeSyncProviderName(TimeSyncProvider provider);

}  // namespace prism_viewer::communication
