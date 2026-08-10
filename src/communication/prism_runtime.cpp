#include "communication/prism_runtime.hpp"

#ifdef _WIN32

#include <Windows.h>

#include <filesystem>
#include <stdexcept>
#include <utility>

#ifndef PRISM_REQUIRED_USB_SDK_VERSION
#error "PRISM_REQUIRED_USB_SDK_VERSION must be provided by the Viewer build"
#endif

namespace prism_runtime {
namespace {

const prism::RuntimeApi* loadApi() {
  static const prism::RuntimeApi* api = [] {
    std::wstring executable_path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(
        nullptr, executable_path.data(),
        static_cast<DWORD>(executable_path.size()));
    if (length == 0 ||
        static_cast<std::size_t>(length) >= executable_path.size()) {
      throw std::runtime_error("cannot resolve Prism Viewer executable path");
    }
    executable_path.resize(length);
    const std::filesystem::path dll_path =
        std::filesystem::path(executable_path).parent_path() /
        L"prism_usb_sdk.dll";
    HMODULE module = LoadLibraryW(dll_path.c_str());
    if (module == nullptr) {
      throw std::runtime_error(
          "LoadLibraryW failed for prism_usb_sdk.dll (Windows error " +
          std::to_string(GetLastError()) + ")");
    }
    const auto get_api = reinterpret_cast<prism::GetRuntimeApiFunction>(
        GetProcAddress(module, prism::kRuntimeApiEntryPoint));
    if (get_api == nullptr) {
      throw std::runtime_error(
          "prism_usb_sdk.dll does not export prism_usb_sdk_get_runtime_api");
    }
    const prism::RuntimeApi* loaded =
        get_api(prism::kRuntimeApiVersion);
    if (loaded == nullptr || loaded->abi_version != prism::kRuntimeApiVersion ||
        loaded->struct_size < sizeof(prism::RuntimeApi) ||
        loaded->sdk_version == nullptr ||
        std::string(loaded->sdk_version) != PRISM_REQUIRED_USB_SDK_VERSION ||
        loaded->msvc_version < 1900) {
      throw std::runtime_error(
          "prism_usb_sdk.dll runtime ABI/version is incompatible with this Viewer");
    }
    // Deliberately retain the module for the process lifetime. Client handles,
    // callbacks, exceptions, and returned C++ objects all use its code/CRT.
    return loaded;
  }();
  return api;
}

}  // namespace

Client::Client() : api_(loadApi()), handle_(api_->client_create()) {
  if (handle_ == nullptr) {
    throw std::runtime_error("SDK client creation failed");
  }
}
Client::~Client() {
  if (handle_ != nullptr) api_->client_destroy(handle_);
}

std::vector<prism::DeviceInfo> Client::enumerate(uint16_t vid, uint16_t pid) {
  return loadApi()->enumerate(vid, pid);
}
void Client::openDevice(const prism::DeviceInfo& d) {
  api_->open_device(handle_, d);
}
void Client::closeDevice() { api_->close_device(handle_); }
bool Client::isOpen() const { return api_->is_open(handle_); }
std::wstring Client::path() const { return api_->path(handle_); }
std::wstring Client::serialNumber() const { return api_->serial_number(handle_); }
bool Client::keepaliveEnabled() const {
  return api_->keepalive_enabled(handle_);
}
bool Client::streamTransferActive() const noexcept {
  return api_->stream_transfer_active(handle_);
}
prism::HelloInfo Client::hello() { return api_->hello(handle_); }
prism::DeviceInfo Client::deviceInfo() { return api_->device_info(handle_); }
prism::DeviceVersions Client::deviceVersions() {
  return api_->device_versions(handle_);
}
prism::SystemTimeSyncResult Client::synchronizeSystemTime(
    uint32_t a, uint32_t b, uint32_t c) {
  return api_->synchronize_system_time(handle_, a, b, c);
}
prism::NetworkInfo Client::networkInfo() { return api_->network_info(handle_); }
prism::WifiHotspotStatus Client::wifiHotspotStatus() {
  return api_->wifi_hotspot_status(handle_);
}
prism::WifiHotspotStatus Client::setWifiHotspotEnabled(bool e) {
  return api_->set_wifi_hotspot_enabled(handle_, e);
}
prism::DeviceConfiguration Client::deviceConfiguration() {
  return api_->device_configuration(handle_);
}
prism::DeviceConfiguration Client::saveDeviceConfiguration(
    const prism::DeviceConfiguration& c, uint32_t m) {
  return api_->save_device_configuration(handle_, c, m);
}
prism::ExposureConfiguration Client::cameraExposure() {
  return api_->camera_exposure(handle_);
}
prism::ExposureConfiguration Client::setExposureConfiguration(
    const prism::ExposureConfiguration& c, uint32_t m) {
  return api_->set_exposure_configuration(handle_, c, m);
}
prism::VideoStatus Client::startVideo1280x1024(uint32_t fps) {
  return api_->start_video(handle_, fps);
}
void Client::stopVideo() { api_->stop_video(handle_); }
void Client::sendVideoAck(uint32_t id) { api_->send_video_ack(handle_, id); }
prism::LidarStatus Client::lidarStatus() { return api_->lidar_status(handle_); }
prism::LidarNetworkStatus Client::lidarNetworkStatus() {
  return api_->lidar_network_status(handle_);
}
prism::LidarNetworkStatus Client::saveLidarNetworkConfiguration(
    const prism::LidarNetworkConfiguration& configuration) {
  return api_->save_lidar_network_configuration(handle_, configuration);
}
prism::LidarNetworkStatus Client::probeLidarNetwork() {
  return api_->probe_lidar_network(handle_);
}
prism::Frame Client::readFrame(uint32_t timeout) {
  return api_->read_frame(handle_, timeout);
}
prism::SystemUpgradeResult Client::upgradeSystem(
    const std::string& p, const prism::UpgradeOptions& o,
    const std::function<void(const prism::SystemUpgradeProgress&)>& cb) {
  return api_->upgrade_system(handle_, p, o, cb);
}

ImuStream::ImuStream(Client& c, prism::ImuSampleHandler h)
    : client_(&c), handler_(std::move(h)) {}
ImuStream::~ImuStream() {
  try {
    stop();
  } catch (...) {
  }
}
void ImuStream::start(uint32_t count, uint32_t rate) {
  if (active_) return;
  if (client_ == nullptr || !client_->isOpen()) {
    throw std::runtime_error("IMU stream client is not open");
  }
  if (!handler_) throw std::runtime_error("IMU stream handler is not set");
  const prism::ImuStreamStatus status =
      client_->api_->start_imu(client_->handle_, count, rate);
  if (status.sensors != count ||
      (rate != 0 && status.nominal_rate_hz != rate)) {
    throw std::runtime_error(
        "agent acknowledged different IMU stream settings");
  }
  active_ = true;
}
void ImuStream::stop() {
  if (!active_) return;
  active_ = false;
  if (client_ != nullptr && client_->isOpen()) {
    client_->api_->stop_imu(client_->handle_);
  }
}
bool ImuStream::active() const noexcept { return active_; }
bool ImuStream::handleFrame(const prism::Frame& f) {
  if (f.type != prism::FrameType::ImuSample) return false;
  if (!active_) return true;
  handler_(client_->api_->parse_imu_sample(f));
  return true;
}

LidarStream::LidarStream(Client& c, prism::LidarPointBatchHandler h)
    : client_(&c), handler_(std::move(h)) {}
LidarStream::~LidarStream() {
  try {
    stop();
  } catch (...) {
  }
}
void LidarStream::start(prism::LidarModel m) {
  if (active_) return;
  if (client_ == nullptr || !client_->isOpen()) {
    throw std::runtime_error("LiDAR stream client is not open");
  }
  if (!handler_) throw std::runtime_error("LiDAR stream handler is not set");
  const prism::LidarStatus status =
      client_->api_->start_lidar(client_->handle_, m);
  if (!status.enabled || status.model != m) {
    throw std::runtime_error("agent acknowledged different LiDAR settings");
  }
  active_ = true;
}
void LidarStream::stop() {
  if (!active_) return;
  active_ = false;
  if (client_ != nullptr && client_->isOpen()) {
    client_->api_->stop_lidar(client_->handle_);
  }
}
bool LidarStream::active() const noexcept { return active_; }
bool LidarStream::handleFrame(const prism::Frame& f) {
  if (f.type != prism::FrameType::LidarPoints) return false;
  if (!active_) return true;
  handler_(client_->api_->parse_lidar_point_batch(f));
  return true;
}

prism::SystemUpgradePackageInfo inspectSystemUpgradePackage(
    const std::string& p) {
  return loadApi()->inspect_system_upgrade_package(p);
}
prism::HeartbeatStatus parseHeartbeat(const prism::Frame& f) {
  return loadApi()->parse_heartbeat(f);
}
prism::VideoChunkView parseVideoChunkView(const prism::Frame& f) {
  return loadApi()->parse_video_chunk_view(f);
}
prism::VideoMeta parseVideoMeta(const prism::Frame& f) {
  return loadApi()->parse_video_meta(f);
}
const char* usbLinkSpeedName(prism::UsbLinkSpeed s) {
  return loadApi()->usb_link_speed_name(s);
}
const char* sensorBoardErrorCodeName(prism::SensorBoardErrorCode c) {
  return loadApi()->sensor_board_error_code_name(c);
}

}  // namespace prism_runtime

#endif
