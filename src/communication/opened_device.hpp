#pragma once
#include "communication/device_session.hpp"
#include "communication/native_camera_control.hpp"
#include "communication/profile_capture.hpp"

namespace prism_viewer::communication {
// Opening a valid legacy DeviceInfo response is not admission to capture.
// Old Agents may omit the model; leave the transport available to the existing
// upgrade API without guessing a product or issuing product-specific commands.
template<class Client, class ReadInfo>
OpenedDevice readOpenedDeviceSnapshot(Client& client, ReadInfo read_info) {
  OpenedDevice opened;
  opened.hello = client.hello();
  opened.versions = client.deviceVersions();
  const auto status = read_info(client);
  opened.device_info = status.info;
  opened.time_sync_provider = status.time_sync_provider;
  opened.serial_number = client.serialNumber();
  opened.path = client.path();
  opened.maintenance_only = !supportsCapture(opened.device_info.device_model);
  if (opened.maintenance_only) return opened;

  opened.configuration = client.deviceConfiguration();
  readProductExposure(client, opened);
  if (!usesProfileCapture(opened.device_info.device_model))
    opened.gnss_timing = client.gnssTimingStatus();
  opened.network = client.networkInfo();
  return opened;
}
} // namespace prism_viewer::communication
