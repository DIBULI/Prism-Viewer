#pragma once

#include <cstdint>

namespace prism {

constexpr uint32_t kDeviceConfigFieldCameraFps = 1u << 0;
constexpr uint32_t kDeviceConfigFieldImuRateHz = 1u << 1;
constexpr uint32_t kDeviceConfigFieldMjpegQuality = 1u << 2;
constexpr uint32_t kDeviceConfigFieldAll =
    kDeviceConfigFieldCameraFps | kDeviceConfigFieldImuRateHz |
    kDeviceConfigFieldMjpegQuality;
constexpr uint32_t kMjpegQualityMin = 1u;
constexpr uint32_t kMjpegQualityMax = 99u;
constexpr uint32_t kMjpegQualityDefault = 88u;

constexpr bool isCameraFpsSupported(uint32_t fps) {
  return fps >= 1u && fps <= 30u;
}

struct DeviceConfiguration {
  uint32_t camera_fps = 30;
  uint32_t imu_rate_hz = 1000;
  uint32_t mjpeg_quality = kMjpegQualityDefault;
  uint32_t generation = 0;
  bool persisted = false;
};

}  // namespace prism
