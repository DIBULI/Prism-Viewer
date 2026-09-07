#include "communication/rtk_corrections.hpp"

#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace prism_viewer::communication {
namespace {

constexpr uint16_t kCorrectionProtocolVersion = 2u;
constexpr uint16_t kNavigationProtocolVersion = 2u;
constexpr size_t kStatusPayloadSize = 96u;
constexpr size_t kNavigationStatusPayloadSize = 248u;
constexpr uint32_t kKnownFlags = 0x7fu;
constexpr uint32_t kKnownNavigationFlags = 0x1fu;
constexpr uint32_t kKnownSmoothingFlags = 0x7fu;

constexpr auto kStatusResponseFrame =
    static_cast<prism::FrameType>(0x95);
constexpr auto kNavigationStatusResponseFrame =
    static_cast<prism::FrameType>(0x96);
constexpr auto kNavigationEventFrame = static_cast<prism::FrameType>(0x97);

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

double readLeDouble(const std::vector<uint8_t>& bytes, size_t offset) {
  const uint64_t bits = readLe64(bytes, offset);
  double value = 0.0;
  static_assert(sizeof(value) == sizeof(bits),
                "wire double must be IEEE-754 binary64");
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

bool validSolution(RtkSolution solution) {
  return solution >= RtkSolution::None && solution <= RtkSolution::Ppp;
}

bool validPosition(double latitude, double longitude, double height,
                   double east_std, double north_std, double up_std) {
  return std::isfinite(latitude) && std::isfinite(longitude) &&
         std::isfinite(height) && std::isfinite(east_std) &&
         std::isfinite(north_std) && std::isfinite(up_std) &&
         latitude >= -90.0 && latitude <= 90.0 && longitude >= -180.0 &&
         longitude <= 180.0 && east_std >= 0.0 && north_std >= 0.0 &&
         up_std >= 0.0;
}

std::string describeRtkNavigationProtocolMismatch(
    const prism::Frame& frame) {
  const std::string received_version =
      frame.payload.size() >= 2u
          ? std::to_string(readLe16(frame.payload, 0))
          : std::string("unavailable");
  const std::string declared_size =
      frame.payload.size() >= 4u
          ? std::to_string(readLe16(frame.payload, 2))
          : std::string("unavailable");
  return "RTK navigation protocol mismatch: received frame type=" +
         std::to_string(static_cast<unsigned int>(frame.type)) +
         ", payload=" + std::to_string(frame.payload.size()) +
         " bytes, version=" + received_version +
         ", declared size=" + declared_size +
         "; Viewer expects response/event type=" +
         std::to_string(static_cast<unsigned int>(
             kNavigationStatusResponseFrame)) +
         "/" +
         std::to_string(static_cast<unsigned int>(kNavigationEventFrame)) +
         ", payload=" + std::to_string(kNavigationStatusPayloadSize) +
         " bytes, version=" +
         std::to_string(kNavigationProtocolVersion) +
         ". Update Viewer and Agent as a matched pair.";
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

RtkNavigationStatus parseViewerRtkNavigationStatus(const prism::Frame& frame) {
  if (!prism_viewer::communication::isViewerRtkNavigationFrame(frame) ||
      frame.payload.size() != kNavigationStatusPayloadSize ||
      readLe16(frame.payload, 0) != kNavigationProtocolVersion ||
      readLe16(frame.payload, 2) != kNavigationStatusPayloadSize) {
    throw std::runtime_error(
        describeRtkNavigationProtocolMismatch(frame));
  }

  RtkNavigationStatus status;
  status.version = readLe16(frame.payload, 0);
  status.flags = readLe32(frame.payload, 4);
  if ((status.flags & ~kKnownNavigationFlags) != 0u) {
    throw std::runtime_error("unknown RTK navigation status flags");
  }
  status.error_code = static_cast<int32_t>(readLe32(frame.payload, 8));
  status.base_source =
      static_cast<RtkBaseSource>(readLe16(frame.payload, 12));
  status.solution = static_cast<RtkSolution>(readLe16(frame.payload, 14));
  status.confidence =
      static_cast<RtkConfidence>(readLe16(frame.payload, 16));
  status.satellites = readLe16(frame.payload, 18);
  status.confidence_score = readLe16(frame.payload, 20);
  if (readLe16(frame.payload, 22) != 0u) {
    throw std::runtime_error("RTK navigation reserved field is non-zero");
  }
  status.confidence_reasons = readLe32(frame.payload, 24);
  status.base_station_id = static_cast<int32_t>(readLe32(frame.payload, 28));
  status.consecutive_fix_epochs = readLe32(frame.payload, 32);
  status.consecutive_float_epochs = readLe32(frame.payload, 36);
  status.solution_epoch_us = static_cast<int64_t>(readLe64(frame.payload, 40));
  status.latitude_deg = readLeDouble(frame.payload, 48);
  status.longitude_deg = readLeDouble(frame.payload, 56);
  status.ellipsoidal_height_m = readLeDouble(frame.payload, 64);
  status.east_std_m = readLeDouble(frame.payload, 72);
  status.north_std_m = readLeDouble(frame.payload, 80);
  status.up_std_m = readLeDouble(frame.payload, 88);
  status.differential_age_s = readLeDouble(frame.payload, 96);
  status.ambiguity_ratio = readLeDouble(frame.payload, 104);
  status.position_jump_m = readLeDouble(frame.payload, 112);
  status.solution_count = readLe64(frame.payload, 120);
  status.fix_count = readLe64(frame.payload, 128);
  status.float_count = readLe64(frame.payload, 136);
  status.rover_observation_epochs = readLe64(frame.payload, 144);
  status.base_observation_epochs = readLe64(frame.payload, 152);
  status.decoder_errors = readLe64(frame.payload, 160);
  status.smoothing_flags = readLe32(frame.payload, 168);
  status.smoothed_solution =
      static_cast<RtkSolution>(readLe16(frame.payload, 172));
  if (readLe16(frame.payload, 174) != 0u) {
    throw std::runtime_error("RTK smoothing reserved field is non-zero");
  }
  status.smoothed_solution_epoch_us =
      static_cast<int64_t>(readLe64(frame.payload, 176));
  status.smoothed_latitude_deg = readLeDouble(frame.payload, 184);
  status.smoothed_longitude_deg = readLeDouble(frame.payload, 192);
  status.smoothed_ellipsoidal_height_m = readLeDouble(frame.payload, 200);
  status.smoothed_east_std_m = readLeDouble(frame.payload, 208);
  status.smoothed_north_std_m = readLeDouble(frame.payload, 216);
  status.smoothed_up_std_m = readLeDouble(frame.payload, 224);
  status.smoothing_reset_count = readLe64(frame.payload, 232);
  status.smoothing_gated_epoch_count = readLe64(frame.payload, 240);
  status.solution_valid = (status.flags & (1u << 0u)) != 0u;
  status.base_position_valid = (status.flags & (1u << 1u)) != 0u;
  status.confidence_valid = (status.flags & (1u << 2u)) != 0u;
  status.position_jump_valid = (status.flags & (1u << 3u)) != 0u;
  status.smoothed_position_valid = (status.flags & (1u << 4u)) != 0u;

  if (status.base_source < RtkBaseSource::None ||
      status.base_source > RtkBaseSource::Ntrip ||
      !validSolution(status.solution) ||
      !validSolution(status.smoothed_solution) ||
      status.confidence < RtkConfidence::Unavailable ||
      status.confidence > RtkConfidence::High ||
      status.confidence_score > 1000u ||
      (status.smoothing_flags & ~kKnownSmoothingFlags) != 0u ||
      (status.smoothing_flags & RtkSmoothingDynamicsEnabled) == 0u) {
    throw std::runtime_error("invalid RTK navigation enum or confidence");
  }
  if (status.solution_valid) {
    if (status.solution == RtkSolution::None ||
        status.solution_epoch_us <= 0 ||
        !validPosition(status.latitude_deg, status.longitude_deg,
                       status.ellipsoidal_height_m, status.east_std_m,
                       status.north_std_m, status.up_std_m) ||
        !std::isfinite(status.differential_age_s) ||
        !std::isfinite(status.ambiguity_ratio) ||
        status.differential_age_s < 0.0 || status.ambiguity_ratio < 0.0) {
      throw std::runtime_error("invalid RTK navigation solution values");
    }
    if (status.position_jump_valid &&
        (!std::isfinite(status.position_jump_m) ||
         status.position_jump_m < 0.0)) {
      throw std::runtime_error("invalid RTK position jump");
    }
  } else if (status.solution != RtkSolution::None) {
    throw std::runtime_error("RTK raw solution validity is inconsistent");
  }
  if (status.smoothed_position_valid) {
    if (status.smoothed_solution == RtkSolution::None ||
        status.smoothed_solution_epoch_us <= 0 ||
        !validPosition(status.smoothed_latitude_deg,
                       status.smoothed_longitude_deg,
                       status.smoothed_ellipsoidal_height_m,
                       status.smoothed_east_std_m,
                       status.smoothed_north_std_m,
                       status.smoothed_up_std_m)) {
      throw std::runtime_error("invalid smoothed RTK navigation values");
    }
  } else if (status.smoothed_solution != RtkSolution::None ||
             status.smoothed_solution_epoch_us != 0) {
    throw std::runtime_error("RTK smoothed solution validity is inconsistent");
  }
  if (status.confidence_valid &&
      status.confidence == RtkConfidence::Unavailable) {
    throw std::runtime_error("RTK confidence validity is inconsistent");
  }
  if ((status.smoothing_flags & RtkSmoothingJumpGated) != 0u &&
      (status.smoothing_flags & RtkSmoothingResetPositionJump) == 0u) {
    throw std::runtime_error("RTK smoothing gate flags are inconsistent");
  }
  return status;
}

bool isViewerRtkNavigationFrame(const prism::Frame& frame) noexcept {
  return frame.type == kNavigationStatusResponseFrame ||
         frame.type == kNavigationEventFrame;
}

RtkNavigationStatus queryRtkNavigationStatus(
    prism_runtime::Client& client) {
  return client.rtkNavigationStatus();
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

const char* rtkConfidenceName(RtkConfidence confidence) {
  switch (confidence) {
    case RtkConfidence::Unavailable: return "unavailable";
    case RtkConfidence::Low: return "low";
    case RtkConfidence::Medium: return "medium";
    case RtkConfidence::High: return "high";
  }
  return "unknown";
}

}  // namespace prism_viewer::communication
