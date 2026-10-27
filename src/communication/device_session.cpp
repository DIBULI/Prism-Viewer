#include "communication/device_session.hpp"
#include "communication/opened_device.hpp"

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
  return readOpenedDevice();
}

OpenedDevice DeviceSession::openNetwork(const prism::NetworkOptions& options) {
  client_.openNetworkDevice(options);
  return readOpenedDevice();
}

OpenedDevice DeviceSession::readOpenedDevice() {
  try {
    return readOpenedDeviceSnapshot(client_, [](auto& client) {
      return readDeviceInfo(client);
    });
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
