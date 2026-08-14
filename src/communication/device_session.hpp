#pragma once

#include "communication/prism_runtime.hpp"

#include <cstddef>
#include <vector>

namespace prism_viewer::communication {

struct OpenedDevice {
  prism::HelloInfo hello;
  prism::DeviceVersions versions;
  prism::DeviceInfo device_info;
  prism::DeviceConfiguration configuration;
  prism::ExposureConfiguration exposure;
  prism::NetworkInfo network;
  std::wstring serial_number;
  std::wstring path;
};

// Owns the USB SDK client and the result of the most recent device scan.
// UI code may orchestrate operations, but device lifetime and enumeration stay
// in this communication module.
class DeviceSession {
 public:
  const std::vector<prism::DeviceInfo>& refresh();
  OpenedDevice open(size_t device_index);
  OpenedDevice openTcp(const std::string& host, uint16_t port);
  void close() noexcept;

  bool isOpen() const;
  prism_runtime::Client& client();
  const std::vector<prism::DeviceInfo>& devices() const;

 private:
  prism_runtime::Client client_;
  std::vector<prism::DeviceInfo> devices_;
};

}  // namespace prism_viewer::communication
