#include "prism/usb/runtime_api.hpp"
#include "prism/usb/gnss_reception_runtime_api.hpp"
#include "prism/usb/network_runtime_api.hpp"
#include "prism/usb/profile_runtime_api.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#include <Windows.h>
#else
extern "C" const prism::RtkModuleControlRuntimeApi* prism_usb_sdk_get_rtk_module_control_api(uint32_t);
extern "C" const prism::RuntimeApi* prism_usb_sdk_get_runtime_api(uint32_t);
extern "C" const prism::GnssReceptionRuntimeApi* prism_usb_sdk_get_gnss_reception_api(uint32_t);
extern "C" const prism::LidarPowerRuntimeApi* prism_usb_sdk_get_lidar_power_api(uint32_t);
extern "C" const prism::NetworkRuntimeApi* prism_usb_sdk_get_network_api(uint32_t);
extern "C" const prism::ProfileRuntimeApi* prism_usb_sdk_get_profile_api(uint32_t);
#endif

int main() {
  try {
    // XT32 metadata changes LidarPoint's layout; reject mixed header/library ABIs.
    static_assert(prism::kRuntimeApiVersion == 19,
                  "Review the Viewer bindings when updating the SDK ABI");
#ifdef _WIN32
    // Load the pinned package, not a DLL supplied by PATH or the host machine.
    const auto module = LoadLibraryW(L"" PRISM_TEST_SDK_RUNTIME);
    if (!module) throw std::runtime_error("Cannot load bundled SDK DLL");
    const auto get_api = reinterpret_cast<prism::GetRuntimeApiFunction>(
        GetProcAddress(module, prism::kRuntimeApiEntryPoint));
#else
    const auto get_api = &prism_usb_sdk_get_runtime_api;
#endif
    if (!get_api) throw std::runtime_error("SDK runtime entry point missing");
    const auto* api = get_api(prism::kRuntimeApiVersion);
    if (!api || api->abi_version != prism::kRuntimeApiVersion ||
        api->struct_size != sizeof(prism::RuntimeApi) || !api->sdk_version ||
        std::string(api->sdk_version) != PRISM_REQUIRED_USB_SDK_VERSION) {
      throw std::runtime_error("SDK headers/runtime version or ABI mismatch");
    }
    if (get_api(0) || get_api(prism::kRuntimeApiVersion - 1) ||
        get_api(prism::kRuntimeApiVersion + 1) ||
        !api->client_create || !api->client_destroy ||
        !api->gnss_timing_status || !api->begin_rtk_corrections ||
        !api->send_rtk_corrections || !api->end_rtk_corrections ||
        !api->rtk_correction_status || !api->timesync_rtk_versions ||
        !api->start_rover_rtcm || !api->stop_rover_rtcm ||
        !api->parse_rover_rtcm_chunk_view) {
      throw std::runtime_error("SDK GPS/RTK/RTCM API is incomplete");
    }
#ifdef _WIN32
    const auto get_reception = reinterpret_cast<prism::GetGnssReceptionRuntimeApiFunction>(
        GetProcAddress(module, prism::kGnssReceptionRuntimeApiEntryPoint));
#else
    const auto get_reception = &prism_usb_sdk_get_gnss_reception_api;
#endif
    const auto* reception = get_reception ? get_reception(1) : nullptr;
    if (!reception || reception->abi_version != 1 ||
        reception->struct_size != sizeof(prism::GnssReceptionRuntimeApi) ||
        !reception->gnss_reception_status || get_reception(0) || get_reception(2)) {
      throw std::runtime_error("Independent reception extension is incomplete");
    }
#ifdef _WIN32
    const auto get_rtk = reinterpret_cast<prism::GetRtkModuleControlRuntimeApiFunction>(
        GetProcAddress(module, prism::kRtkModuleControlRuntimeApiEntryPoint));
#else
    const auto get_rtk = &prism_usb_sdk_get_rtk_module_control_api;
#endif
    const auto* rtk = get_rtk ? get_rtk(1) : nullptr;
    if (!rtk || rtk->abi_version != 1 || rtk->struct_size < sizeof(*rtk) ||
        !rtk->save_cors_configuration || !rtk->start_rtk || !rtk->stop_rtk || get_rtk(0) || get_rtk(2))
      throw std::runtime_error("Timesync RTK control extension is incomplete");
    // Validate the independent power extension without accessing hardware.
#ifdef _WIN32
    const auto get_power = reinterpret_cast<prism::GetLidarPowerRuntimeApiFunction>(
        GetProcAddress(module, prism::kLidarPowerRuntimeApiEntryPoint));
#else
    const auto get_power = &prism_usb_sdk_get_lidar_power_api;
#endif
    const auto* power = get_power ? get_power(1) : nullptr;
    if (!power || power->abi_version != 1 ||
        power->struct_size != sizeof(prism::LidarPowerRuntimeApi) ||
        !power->query || !power->set_standby || get_power(0) || get_power(2)) {
      throw std::runtime_error("Independent LiDAR power extension is incomplete");
    }
    // Require the cap-aware network extension in release packages as well.
#ifdef _WIN32
    const auto get_network = reinterpret_cast<prism::GetNetworkRuntimeApiFunction>(
        GetProcAddress(module, prism::kNetworkRuntimeApiEntryPoint));
#else
    const auto get_network = &prism_usb_sdk_get_network_api;
#endif
    const auto* network = get_network ? get_network(prism::kNetworkRuntimeApiVersion) : nullptr;
    if (!network || network->abi_version != 4 || network->struct_size != sizeof(*network) ||
        !network->open || !network->is_network || !network->camera_max_fps)
      throw std::runtime_error("Matching SDK network API v4 with Web password is required");
#ifdef _WIN32
    const auto get_profile = reinterpret_cast<prism::GetProfileRuntimeApiFunction>(
        GetProcAddress(module, prism::kProfileRuntimeApiEntryPoint));
#else
    const auto get_profile = &prism_usb_sdk_get_profile_api;
#endif
    const auto* profile = get_profile ? get_profile(1) : nullptr;
    if (!profile || profile->abi_version != 1 || profile->struct_size != sizeof(*profile) ||
        !profile->start_415m || !profile->start_mixed_gpu || !profile->begin_live_rows ||
        !profile->finish_live_rows || !profile->live_rows_pending || get_profile(0) || get_profile(2))
      throw std::runtime_error("Matching profile capture/live-row extension is required");
    // No enumeration, connection, capture, or time/configuration mutation.
    auto* client = api->client_create();
    if (!client) throw std::runtime_error("Cannot create SDK client");
    api->client_destroy(client);
    std::cout << "Prism SDK " << api->sdk_version << " | Runtime API "
              << api->abi_version << " | GPS/RTK/RTCM bindings OK\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
