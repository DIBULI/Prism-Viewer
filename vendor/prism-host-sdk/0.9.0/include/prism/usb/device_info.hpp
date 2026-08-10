#pragma once

#include "prism/usb/common.hpp"

namespace prism {

constexpr uint16_t kDeviceInfoProtocolVersion = 3;
constexpr uint16_t kDeviceInfoPayloadSize = 256;

// Strict parser for the current fixed-size DeviceInfo response.
DeviceInfo parseDeviceInfo(const Frame& frame);

const char* usbLinkSpeedName(UsbLinkSpeed speed);
const char* imuInitErrorReasonName(ImuInitErrorReason reason);
const char* sensorBoardErrorCodeName(SensorBoardErrorCode code);

}  // namespace prism
