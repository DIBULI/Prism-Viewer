#include "communication/device_session.hpp"

#include <stdexcept>
#include <string>

namespace {

bool exposureLimitsUnsupported(const std::exception& exception) {
  return std::string(exception.what()).find("unknown message type") !=
         std::string::npos;
}

}  // namespace

namespace prism_viewer::communication {

const std::vector<prism::DeviceInfo>& DeviceSession::refresh() {
  if (client_.isOpen()) {
    throw std::logic_error("cannot scan devices while a device is open");
  }
  devices_ = prism_runtime::Client::enumerate();
  return devices_;
}

OpenedDevice DeviceSession::open(size_t device_index) {
  if (device_index >= devices_.size()) {
    throw std::out_of_range("selected USB device no longer exists");
  }

  client_.openDevice(devices_[device_index]);
  try {
    OpenedDevice opened;
    opened.hello = client_.hello();
    opened.versions = client_.deviceVersions();
    opened.device_info = client_.deviceInfo();
    opened.configuration = client_.deviceConfiguration();
    opened.exposure = client_.cameraExposure();
    try {
      opened.exposure_limits = client_.cameraExposureLimits();
    } catch (const std::exception& exception) {
      if (!exposureLimitsUnsupported(exception)) throw;
      // Exposure-limit commands were added after the original 1.0 agent
      // protocol. Keep the already-open handle and use the SDK defaults when
      // an older device reports that this optional message is unknown.
      opened.exposure_limits = prism::ExposureLimits{};
      opened.exposure_limits_supported = false;
    }
    opened.network = client_.networkInfo();
    opened.serial_number = client_.serialNumber();
    opened.path = client_.path();
    return opened;
  } catch (...) {
    client_.closeDevice();
    throw;
  }
}

void DeviceSession::close() noexcept {
  try {
    client_.closeDevice();
  } catch (...) {
  }
}

bool DeviceSession::isOpen() const {
  return client_.isOpen();
}

prism_runtime::Client& DeviceSession::client() {
  return client_;
}

const std::vector<prism::DeviceInfo>& DeviceSession::devices() const {
  return devices_;
}

}  // namespace prism_viewer::communication
