#pragma once
#include "communication/device_session.hpp"
#include "communication/native_camera_control.hpp"
#include "communication/profile_capture.hpp"

namespace prism_viewer::communication {
// Opening a valid legacy DeviceInfo response is not admission to capture.
// Old Agents may omit the model; leave the transport available to the existing
// upgrade API without guessing a product or issuing product-specific commands.
template<class Client, class ReadInfo>
OpenedDevice readOpenedDeviceSnapshot(Client& client, ReadInfo read_info,
    prism::DeviceModel legacy_model=prism::DeviceModel::Unknown) {
  OpenedDevice opened;
  opened.hello = client.hello();
  try{opened.versions = client.deviceVersions();}
  catch(const std::exception& error){if(!unsupportedFeature(error))throw;opened.versions.agent=opened.hello.version;}
  const auto status = read_info(client);
  opened.device_info = status.info;
  opened.time_sync_provider = status.time_sync_provider;
  opened.serial_number = client.serialNumber();
  opened.path = client.path();
  if(legacy_model!=prism::DeviceModel::Unknown){
    if((opened.device_info.info_version!=3&&opened.device_info.info_version!=4)||
        opened.device_info.device_model!=prism::DeviceModel::Unknown||!supportsCapture(legacy_model))
      throw std::runtime_error("Model selection is only available for legacy DeviceInfo v3/v4");
    opened.device_info.device_model=legacy_model;opened.legacy_model_selected=true;
  }
  opened.maintenance_only = !supportsCapture(opened.device_info.device_model);
  if (opened.maintenance_only) return opened;

  opened.configuration = client.deviceConfiguration();
  readProductExposure(client, opened);
  if (!usesProfileCapture(opened.device_info.device_model)){
    try{opened.gnss_timing = client.gnssTimingStatus();}
    catch(const std::exception& error){if(!unsupportedFeature(error))throw;}
  }
  try{opened.network = client.networkInfo();}
  catch(const std::exception& error){if(!unsupportedFeature(error))throw;}
  return opened;
}
} // namespace prism_viewer::communication
