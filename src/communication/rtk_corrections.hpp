#pragma once

#include "communication/prism_runtime.hpp"

#include <cstddef>
#include <cstdint>

namespace prism_viewer::communication {

enum class RtkBaseSource : uint16_t {
  None = 0,
  HostCors = 1,
  LocalSocket = 2,
  Ntrip = 3,
};

enum class RtkSolution : uint16_t {
  None = 0,
  Single = 1,
  Dgps = 2,
  Float = 3,
  Fix = 4,
  Ppp = 5,
};

enum class RtkConfidence : uint16_t {
  Unavailable = 0,
  Low = 1,
  Medium = 2,
  High = 3,
};

struct RtkCorrectionStatus {
  uint16_t version = 0;
  uint32_t flags = 0;
  int32_t error_code = 0;
  bool running = false;
  bool rover_connected = false;
  bool base_connected = false;
  bool host_active = false;
  bool base_position_valid = false;
  bool ntrip_configured = false;
  bool ntrip_connected = false;
  RtkBaseSource base_source = RtkBaseSource::None;
  RtkSolution solution = RtkSolution::None;
  uint64_t host_correction_bytes = 0;
  uint64_t rover_bytes = 0;
  uint64_t base_bytes = 0;
  uint64_t base_rtcm_messages = 0;
  uint64_t base_observation_epochs = 0;
  uint64_t solution_count = 0;
  uint64_t fix_count = 0;
  uint64_t float_count = 0;
  uint64_t decoder_errors = 0;
};

// Complete latest-solution snapshot reported by the extended Agent protocol.
// This is intentionally a separate query from the legacy 88-byte correction
// status so older Agents and Host SDKs remain wire-compatible.
struct RtkNavigationStatus {
  uint16_t version = 0;
  uint32_t flags = 0;
  int32_t error_code = 0;
  bool solution_valid = false;
  bool base_position_valid = false;
  bool confidence_valid = false;
  bool position_jump_valid = false;
  RtkBaseSource base_source = RtkBaseSource::None;
  RtkSolution solution = RtkSolution::None;
  RtkConfidence confidence = RtkConfidence::Unavailable;
  uint16_t satellites = 0;
  uint16_t confidence_score = 0;
  uint32_t confidence_reasons = 0;
  int32_t base_station_id = 0;
  uint32_t consecutive_fix_epochs = 0;
  uint32_t consecutive_float_epochs = 0;
  int64_t solution_epoch_us = 0;
  double latitude_deg = 0.0;
  double longitude_deg = 0.0;
  double ellipsoidal_height_m = 0.0;
  double east_std_m = 0.0;
  double north_std_m = 0.0;
  double up_std_m = 0.0;
  double differential_age_s = 0.0;
  double ambiguity_ratio = 0.0;
  double position_jump_m = 0.0;
  uint64_t solution_count = 0;
  uint64_t fix_count = 0;
  uint64_t float_count = 0;
  uint64_t rover_observation_epochs = 0;
  uint64_t base_observation_epochs = 0;
  uint64_t decoder_errors = 0;
};

RtkCorrectionStatus parseRtkCorrectionStatus(const prism::Frame& frame);
RtkCorrectionStatus beginRtkCorrections(prism_runtime::Client& client);
RtkCorrectionStatus sendRtkCorrections(prism_runtime::Client& client,
                                       const uint8_t* data, size_t size,
                                       uint32_t timeout_ms = 3000);
RtkCorrectionStatus endRtkCorrections(prism_runtime::Client& client);
RtkCorrectionStatus queryRtkCorrectionStatus(
    prism_runtime::Client& client);
RtkNavigationStatus parseRtkNavigationStatus(const prism::Frame& frame);
bool isRtkNavigationFrame(const prism::Frame& frame) noexcept;
RtkNavigationStatus queryRtkNavigationStatus(
    prism_runtime::Client& client);

const char* rtkSolutionName(RtkSolution solution);
const char* rtkBaseSourceName(RtkBaseSource source);
const char* rtkConfidenceName(RtkConfidence confidence);

}  // namespace prism_viewer::communication
