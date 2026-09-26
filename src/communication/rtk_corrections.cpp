#include "communication/rtk_corrections.hpp"

#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace prism_viewer::communication {
namespace {

constexpr uint16_t kCorrectionProtocolVersion = 2u;
constexpr size_t kStatusPayloadSize = 96u;
constexpr uint32_t kKnownFlags = 0x7fu;

constexpr auto kStatusResponseFrame =
    static_cast<prism::FrameType>(0x95);

uint16_t readLe16(const std::vector<uint8_t>& bytes, size_t offset) {
  return static_cast<uint16_t>(bytes.at(offset)) |
         (static_cast<uint16_t>(bytes.at(offset + 1)) << 8u);
}

uint32_t readLe32(const std::vector<uint8_t>& bytes, size_t offset) {
  return static_cast<uint32_t>(bytes.at(offset)) |
         (static_cast<uint32_t>(bytes.at(offset + 1)) << 8u) |
         (static_cast<uint32_t>(bytes.at(offset + 2)) << 16u) |
         (static_cast<uint32_t>(bytes.at(offset + 3)) << 24u);
}

uint64_t readLe64(const std::vector<uint8_t>& bytes, size_t offset) {
  uint64_t value = 0;
  for (size_t index = 0; index < 8u; ++index) {
    value |= static_cast<uint64_t>(bytes.at(offset + index))
             << (index * 8u);
  }
  return value;
}





}  // namespace

RtkCorrectionStatus parseRtkCorrectionStatus(const prism::Frame& frame) {
  if (frame.type != kStatusResponseFrame ||
      frame.payload.size() != kStatusPayloadSize ||
      readLe16(frame.payload, 0) != kCorrectionProtocolVersion ||
      readLe16(frame.payload, 2) != kStatusPayloadSize) {
    throw std::runtime_error("not an RTK correction status response");
  }

  RtkCorrectionStatus status;
  status.version = readLe16(frame.payload, 0);
  status.flags = readLe32(frame.payload, 4);
  if ((status.flags & ~kKnownFlags) != 0u) {
    throw std::runtime_error("unknown RTK correction status flags");
  }
  status.error_code = static_cast<int32_t>(readLe32(frame.payload, 8));
  status.base_source =
      static_cast<RtkBaseSource>(readLe16(frame.payload, 12));
  status.solution = static_cast<RtkSolution>(readLe16(frame.payload, 14));
  if (status.base_source < RtkBaseSource::None ||
      status.base_source > RtkBaseSource::Ntrip) {
    throw std::runtime_error("invalid RTK base source");
  }
  if (status.solution < RtkSolution::None ||
      status.solution > RtkSolution::Ppp) {
    throw std::runtime_error("invalid RTK solution");
  }
  status.host_correction_bytes = readLe64(frame.payload, 16);
  status.rover_bytes = readLe64(frame.payload, 24);
  status.base_bytes = readLe64(frame.payload, 32);
  status.base_rtcm_messages = readLe64(frame.payload, 40);
  status.base_observation_epochs = readLe64(frame.payload, 48);
  status.solution_count = readLe64(frame.payload, 56);
  status.fix_count = readLe64(frame.payload, 64);
  status.float_count = readLe64(frame.payload, 72);
  status.decoder_errors = readLe64(frame.payload, 80);
  status.correction_format =
      static_cast<RtkCorrectionFormat>(readLe16(frame.payload, 88));
  if (status.correction_format < RtkCorrectionFormat::Unknown ||
      status.correction_format > RtkCorrectionFormat::Unsupported) {
    throw std::runtime_error("invalid RTK correction format");
  }
  status.running = (status.flags & (1u << 0u)) != 0u;
  status.rover_connected = (status.flags & (1u << 1u)) != 0u;
  status.base_connected = (status.flags & (1u << 2u)) != 0u;
  status.host_active = (status.flags & (1u << 3u)) != 0u;
  status.base_position_valid = (status.flags & (1u << 4u)) != 0u;
  status.ntrip_configured = (status.flags & (1u << 5u)) != 0u;
  status.ntrip_connected = (status.flags & (1u << 6u)) != 0u;
  if (status.host_active && status.base_source != RtkBaseSource::HostCors) {
    throw std::runtime_error("inconsistent Host CORS status");
  }
  return status;
}

RtkCorrectionStatus beginRtkCorrections(prism_runtime::Client& client) {
  return client.beginRtkCorrections();
}

RtkCorrectionStatus sendRtkCorrections(prism_runtime::Client& client,
                                     const uint8_t* data, size_t size,
                                     uint32_t timeout_ms) {
  if (data == nullptr || size == 0u) {
    throw std::invalid_argument("RTK correction data must not be empty");
  }
  return client.sendRtkCorrections(data, size, timeout_ms);
}

RtkCorrectionStatus endRtkCorrections(prism_runtime::Client& client) {
  return client.endRtkCorrections();
}

RtkCorrectionStatus queryRtkCorrectionStatus(prism_runtime::Client& client) {
  return client.rtkCorrectionStatus();
}




const char* rtkSolutionName(RtkSolution solution) {
  switch (solution) {
    case RtkSolution::None: return "none";
    case RtkSolution::Single: return "single";
    case RtkSolution::Dgps: return "DGPS";
    case RtkSolution::Float: return "float";
    case RtkSolution::Fix: return "fix";
    case RtkSolution::Ppp: return "PPP";
  }
  return "unknown";
}

const char* rtkBaseSourceName(RtkBaseSource source) {
  switch (source) {
    case RtkBaseSource::None: return "none";
    case RtkBaseSource::HostCors: return "Host CORS";
    case RtkBaseSource::LocalSocket: return "local socket";
    case RtkBaseSource::Ntrip: return "Agent NTRIP";
  }
  return "unknown";
}

const char* rtkCorrectionFormatName(RtkCorrectionFormat format) {
  switch (format) {
    case RtkCorrectionFormat::Unknown: return "unknown";
    case RtkCorrectionFormat::Rtcm2: return "RTCM 2.x";
    case RtkCorrectionFormat::Rtcm3: return "RTCM 3.x";
    case RtkCorrectionFormat::Unsupported: return "unsupported";
  }
  return "unknown";
}


}  // namespace prism_viewer::communication
