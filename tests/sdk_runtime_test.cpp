#include "prism/usb/runtime_api.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#include <Windows.h>
#else
extern "C" const prism::RuntimeApi* prism_usb_sdk_get_runtime_api(uint32_t);
#endif

int main() {
  try {
    static_assert(prism::kRuntimeApiVersion == 12,
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
    if (get_api(0) || !api->client_create || !api->client_destroy ||
        !api->gnss_timing_status || !api->begin_rtk_corrections ||
        !api->send_rtk_corrections || !api->end_rtk_corrections ||
        !api->rtk_correction_status ||
        !api->rtk_navigation_status || !api->parse_rtk_navigation_status ||
        !api->start_rover_rtcm || !api->stop_rover_rtcm ||
        !api->parse_rover_rtcm_chunk_view) {
      throw std::runtime_error("SDK GPS/RTK/RTCM API is incomplete");
    }
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
