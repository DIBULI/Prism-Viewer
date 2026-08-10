#pragma once

#include <cstdint>
#include <string>

#include "prism/usb/common.hpp"

namespace prism {

struct UpgradeStatus {
  uint16_t code = 0;
  uint16_t state = 0;
  uint32_t received = 0;
  uint32_t total_size = 0;
  std::string message;
  bool restart_verified = false;
  bool rolled_back = false;
  std::string previous_version;
  std::string installed_version;
};

struct UpgradeOptions {
  // upgradeSystem() takes both component versions from manifest.ini. This
  // field is reserved for the SDK's internal component transactions.
  std::string version;
  uint32_t chunk_size = 256 * 1024;
  bool wait_for_restart = true;
  uint32_t restart_timeout_ms = 45000;
};

struct SensorBoardUpgradeStatus {
  uint16_t code = 0;
  uint16_t state = 0;
  uint32_t received = 0;         // Bytes staged on RK.
  uint32_t total_size = 0;
  uint32_t device_received = 0;  // Bytes acknowledged by the sensor-board.
  std::string message;
};

enum class SystemUpgradePhase : uint16_t {
  ValidatingPackage = 0,
  Agent = 1,
  SensorBoard = 2,
  Complete = 3,
};

struct SystemUpgradePackageInfo {
  uint16_t format_version = 0;
  std::string package_version;
  std::string agent_version;
  std::string sensor_board_version;
  uint64_t agent_size = 0;
  uint64_t sensor_board_size = 0;
  std::string agent_sha256;
  std::string sensor_board_sha256;
};

struct SystemUpgradeProgress {
  SystemUpgradePhase phase = SystemUpgradePhase::ValidatingPackage;
  // Transfer work includes the sensor-board image twice: host -> RK staging,
  // then RK -> sensor-board. This keeps the overall progress monotonic.
  uint64_t completed_bytes = 0;
  uint64_t total_bytes = 0;
  uint64_t component_received = 0;
  uint64_t component_total = 0;
  std::string message;
};

struct SystemUpgradeResult {
  SystemUpgradePackageInfo package;
  UpgradeStatus agent;
  SensorBoardUpgradeStatus sensor_board;
  bool complete = false;
};

// Validates a Prism system update ZIP without opening a USB device. The
// archive must contain manifest.ini, prism-agent, and BOOT.BIN.
SystemUpgradePackageInfo inspectSystemUpgradePackage(
    const std::string& package_path);

UpgradeStatus parseUpgradeStatus(const Frame& frame);
SensorBoardUpgradeStatus parseSensorBoardUpgradeStatus(const Frame& frame);

}  // namespace prism
