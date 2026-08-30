#include "communication/device_session.hpp"

#include <stdexcept>

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
    const DeviceInfoStatus device_info = readDeviceInfo(client_);
    opened.device_info = device_info.info;
    opened.time_sync_provider = device_info.time_sync_provider;
    opened.configuration = client_.deviceConfiguration();
    opened.exposure = client_.cameraExposure();
    opened.exposure_limits = client_.cameraExposureLimits();
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
