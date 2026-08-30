#include "communication/device_info_compat.hpp"

#include "prism/usb/device_info.hpp"

#include <cstdint>
#include <stdexcept>
#include <vector>

namespace {

using prism_viewer::communication::TimeSyncProvider;
using prism_viewer::communication::parseCompatibleDeviceInfo;

void require(bool condition) {
  if (!condition) {
    throw std::runtime_error("DeviceInfo compatibility assertion failed");
  }
}

void put16(std::vector<uint8_t>* data, size_t offset, uint16_t value) {
  (*data)[offset] = static_cast<uint8_t>(value & 0xffu);
  (*data)[offset + 1u] = static_cast<uint8_t>(value >> 8u);
}

void put32(std::vector<uint8_t>* data, size_t offset, uint32_t value) {
  for (size_t index = 0; index < 4u; ++index) {
    (*data)[offset + index] =
        static_cast<uint8_t>(value >> (index * 8u));
  }
}

prism::Frame validFrame(uint16_t version, TimeSyncProvider provider) {
  prism::Frame frame;
  frame.type = prism::FrameType::DeviceInfoResponse;
  frame.payload.assign(prism::kDeviceInfoPayloadSize, 0u);
  put16(&frame.payload, 0u, version);
  put16(&frame.payload, 2u, prism::kDeviceInfoPayloadSize);

  uint32_t flags = (1u << 0u) | (1u << 1u);
  if (provider != TimeSyncProvider::Unsynced) {
    flags |= (1u << 2u);
  }
  put32(&frame.payload, 4u, flags);
  frame.payload[8] =
      static_cast<uint8_t>(prism::UsbLinkSpeed::UsbSuperSpeed);
  frame.payload[9] = 2u;
  frame.payload[10] = 4u;
  frame.payload[11] = 0x03u;
  frame.payload[12] = 0x03u;
  frame.payload[13] =
      provider == TimeSyncProvider::Unsynced ? 0u : 0x03u;
  frame.payload[15] = 0x0fu;
  frame.payload[16] = 0x0fu;
  put16(&frame.payload, 20u,
        version == 3u ? prism::kOnboardImuRateHz : 1000u);
  put16(&frame.payload, 22u, 30u);
  if (version == 4u) {
    frame.payload[254] = static_cast<uint8_t>(provider);
  }
  return frame;
}

bool rejected(const prism::Frame& frame) {
  try {
    (void)parseCompatibleDeviceInfo(frame);
    return false;
  } catch (const std::runtime_error&) {
    return true;
  }
}

}  // namespace

int main() {
  auto status =
      parseCompatibleDeviceInfo(validFrame(4u, TimeSyncProvider::Gps));
  require(status.time_sync_provider == TimeSyncProvider::Gps);
  require(status.info.sensor_board_time_synced);
  require(status.info.imu_time_synced_mask == 0x03u);

  status =
      parseCompatibleDeviceInfo(validFrame(4u, TimeSyncProvider::RkPtp));
  require(status.time_sync_provider == TimeSyncProvider::RkPtp);
  require(status.info.sensor_board_time_synced);

  status =
      parseCompatibleDeviceInfo(validFrame(4u, TimeSyncProvider::Unsynced));
  require(status.time_sync_provider == TimeSyncProvider::Unsynced);
  require(!status.info.sensor_board_time_synced);

  status =
      parseCompatibleDeviceInfo(validFrame(3u, TimeSyncProvider::RkPtp));
  require(status.time_sync_provider == TimeSyncProvider::LegacyUnknown);
  require(status.info.sensor_board_time_synced);

  status =
      parseCompatibleDeviceInfo(validFrame(3u, TimeSyncProvider::Unsynced));
  require(status.time_sync_provider == TimeSyncProvider::Unsynced);

  auto invalid = validFrame(4u, TimeSyncProvider::Gps);
  invalid.payload[254] = 3u;
  require(rejected(invalid));

  invalid = validFrame(4u, TimeSyncProvider::Gps);
  put32(&invalid.payload, 4u, (1u << 0u) | (1u << 1u));
  require(rejected(invalid));

  invalid = validFrame(5u, TimeSyncProvider::Gps);
  require(rejected(invalid));

  return 0;
}
