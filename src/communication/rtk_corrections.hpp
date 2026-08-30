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

RtkCorrectionStatus parseRtkCorrectionStatus(const prism::Frame& frame);
RtkCorrectionStatus beginRtkCorrections(prism_runtime::Client& client);
RtkCorrectionStatus sendRtkCorrections(prism_runtime::Client& client,
                                       const uint8_t* data, size_t size,
                                       uint32_t timeout_ms = 3000);
RtkCorrectionStatus endRtkCorrections(prism_runtime::Client& client);
RtkCorrectionStatus queryRtkCorrectionStatus(
    prism_runtime::Client& client);

const char* rtkSolutionName(RtkSolution solution);
const char* rtkBaseSourceName(RtkBaseSource source);

}  // namespace prism_viewer::communication
