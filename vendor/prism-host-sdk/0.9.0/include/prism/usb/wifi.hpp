#pragma once

#include <cstdint>
#include <string>

#include "prism/usb/common.hpp"

namespace prism {

constexpr uint16_t kWifiHotspotProtocolVersion = 1;
constexpr uint16_t kWifiHotspotSetPayloadSize = 8;
constexpr uint16_t kWifiHotspotStatusPayloadSize = 208;

constexpr uint32_t kWifiHotspotFlagPresent = 1u << 0;
constexpr uint32_t kWifiHotspotFlagEnabled = 1u << 1;
constexpr uint32_t kWifiHotspotFlagAccessPointRunning = 1u << 2;
constexpr uint32_t kWifiHotspotFlagDhcpRunning = 1u << 3;
constexpr uint32_t kWifiHotspotFlagPersisted = 1u << 4;
constexpr uint32_t kWifiHotspotFlagAll =
    kWifiHotspotFlagPresent | kWifiHotspotFlagEnabled |
    kWifiHotspotFlagAccessPointRunning | kWifiHotspotFlagDhcpRunning |
    kWifiHotspotFlagPersisted;

struct WifiHotspotStatus {
  uint16_t version = 0;
  uint16_t size = 0;
  uint32_t flags = 0;
  bool present = false;
  bool enabled = false;
  // True only when both the access point and its DHCP service are running.
  bool running = false;
  bool ap_running = false;
  bool dhcp_running = false;
  bool persisted = false;
  int32_t error_code = 0;
  std::string interface_name;
  std::string ssid;
  std::string address;
  std::string error;
};

// Strictly parses the current fixed-size protocol-1 WiFi hotspot status.
WifiHotspotStatus parseWifiHotspotStatus(const Frame& frame);

}  // namespace prism
