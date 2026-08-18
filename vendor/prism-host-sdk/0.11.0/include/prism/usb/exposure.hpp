#pragma once

#include <array>
#include <cstdint>

#include "prism/usb/common.hpp"

namespace prism {

constexpr uint16_t kExposureProtocolVersion = 2;
constexpr uint16_t kExposurePayloadSize = 44;

constexpr uint32_t kExposureFieldTargetBrightness = 1u << 0;
constexpr uint32_t kExposureFieldCamera0 = 1u << 1;
constexpr uint32_t kExposureFieldCamera1 = 1u << 2;
constexpr uint32_t kExposureFieldCamera2 = 1u << 3;
constexpr uint32_t kExposureFieldCamera3 = 1u << 4;
constexpr uint32_t kExposureFieldCameraAll =
    kExposureFieldCamera0 | kExposureFieldCamera1 |
    kExposureFieldCamera2 | kExposureFieldCamera3;
constexpr uint32_t kExposureFieldAll =
    kExposureFieldTargetBrightness | kExposureFieldCameraAll;

constexpr uint8_t kCameraAutomaticMaskAll = 0x0f;
constexpr uint8_t kAutoExposureMinTargetBrightness = 1;
constexpr uint8_t kAutoExposureMaxTargetBrightness = 255;
constexpr uint8_t kAutoExposureDefaultTargetBrightness = 35;
constexpr uint32_t kCameraMinExposureUs = 200;
constexpr uint32_t kCameraMaxExposureUs = 995000;
constexpr uint32_t kCameraExposureHeadroomUs = 5000;
constexpr uint32_t kCameraDefaultExposureUs = 200;
constexpr uint32_t kCameraMinGainX1024 = 1024;
constexpr uint32_t kCameraMaxGainX1024 = 126976;
constexpr uint32_t kCameraGainStepX1024 = 32;
constexpr uint32_t kCameraDefaultGainX1024 = 1024;

constexpr uint32_t cameraMaxExposureUs(uint32_t camera_fps) {
  return camera_fps >= 1u && camera_fps <= 30u
             ? 1000000u / camera_fps - kCameraExposureHeadroomUs
             : 0u;
}

enum class CameraExposureMode : uint8_t {
  Automatic = 0,
  Manual = 1,
};

struct CameraExposureConfiguration {
  CameraExposureMode mode = CameraExposureMode::Automatic;
  uint32_t exposure_time_us = kCameraDefaultExposureUs;
  uint32_t gain_x1024 = kCameraDefaultGainX1024;
};

// Runtime-only state. It is intentionally not part of DeviceConfiguration and
// is never persisted by the agent.
struct ExposureConfiguration {
  uint8_t automatic_camera_mask = kCameraAutomaticMaskAll;
  uint8_t target_brightness = kAutoExposureDefaultTargetBrightness;
  std::array<uint32_t, 4> manual_exposure_time_us{
      kCameraDefaultExposureUs, kCameraDefaultExposureUs,
      kCameraDefaultExposureUs, kCameraDefaultExposureUs};
  std::array<uint32_t, 4> gain_x1024{
      kCameraDefaultGainX1024, kCameraDefaultGainX1024,
      kCameraDefaultGainX1024, kCameraDefaultGainX1024};
};

// Strictly parses the current fixed-size runtime exposure response.
ExposureConfiguration parseExposureConfiguration(const Frame& frame);

}  // namespace prism
