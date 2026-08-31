#include "communication/rtk_corrections.hpp"

#include <iostream>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace {
void writeLe(std::vector<uint8_t>* bytes, size_t offset,
             uint64_t value, size_t width) {
  for (size_t index = 0; index < width; ++index) {
    bytes->at(offset + index) =
        static_cast<uint8_t>((value >> (index * 8u)) & 0xffu);
  }
}
bool require(bool condition, const char* message) {
  if (!condition) std::cerr << message << '\n';
  return condition;
}
void writeDouble(std::vector<uint8_t>* bytes, size_t offset, double value) {
  uint64_t bits = 0;
  static_assert(sizeof(bits) == sizeof(value), "unexpected double width");
  std::memcpy(&bits, &value, sizeof(bits));
  writeLe(bytes, offset, bits, sizeof(bits));
}
}  // namespace

int main() {
  using namespace prism_viewer::communication;
  prism::Frame frame;
  frame.type = static_cast<prism::FrameType>(0x95);
  frame.payload.assign(88u, 0u);
  writeLe(&frame.payload, 0, 1u, 2u);
  writeLe(&frame.payload, 2, 88u, 2u);
  writeLe(&frame.payload, 4, 0x1fu, 4u);
  writeLe(&frame.payload, 12,
          static_cast<uint16_t>(RtkBaseSource::HostCors), 2u);
  writeLe(&frame.payload, 14,
          static_cast<uint16_t>(RtkSolution::Fix), 2u);
  writeLe(&frame.payload, 16, 1234u, 8u);
  writeLe(&frame.payload, 24, 5678u, 8u);
  writeLe(&frame.payload, 32, 4321u, 8u);
  writeLe(&frame.payload, 40, 42u, 8u);
  writeLe(&frame.payload, 48, 21u, 8u);
  writeLe(&frame.payload, 56, 20u, 8u);
  writeLe(&frame.payload, 64, 18u, 8u);
  writeLe(&frame.payload, 72, 2u, 8u);
  writeLe(&frame.payload, 80, 1u, 8u);

  bool ok = true;
  const auto status = parseRtkCorrectionStatus(frame);
  ok &= require(status.running && status.rover_connected &&
                    status.base_connected && status.host_active &&
                    status.base_position_valid,
                "RTK status flags were not parsed");
  ok &= require(status.base_source == RtkBaseSource::HostCors,
                "RTK base source was not parsed");
  ok &= require(status.solution == RtkSolution::Fix,
                "RTK solution was not parsed");
  ok &= require(status.host_correction_bytes == 1234u &&
                    status.base_rtcm_messages == 42u &&
                    status.fix_count == 18u &&
                    status.decoder_errors == 1u,
                "RTK counters were not parsed");

  frame.payload.at(4) |= 0x80u;
  bool rejected = false;
  try {
    (void)parseRtkCorrectionStatus(frame);
  } catch (const std::runtime_error&) {
    rejected = true;
  }
  ok &= require(rejected, "Unknown RTK status flags were accepted");

  prism::Frame navigation;
  navigation.type = static_cast<prism::FrameType>(0x96);
  navigation.payload.assign(168u, 0u);
  writeLe(&navigation.payload, 0, 1u, 2u);
  writeLe(&navigation.payload, 2, 168u, 2u);
  writeLe(&navigation.payload, 4, 0x0fu, 4u);
  writeLe(&navigation.payload, 12,
          static_cast<uint16_t>(RtkBaseSource::HostCors), 2u);
  writeLe(&navigation.payload, 14,
          static_cast<uint16_t>(RtkSolution::Fix), 2u);
  writeLe(&navigation.payload, 16,
          static_cast<uint16_t>(RtkConfidence::High), 2u);
  writeLe(&navigation.payload, 18, 17u, 2u);
  writeLe(&navigation.payload, 20, 932u, 2u);
  writeLe(&navigation.payload, 24, 0x12u, 4u);
  writeLe(&navigation.payload, 28, 42u, 4u);
  writeLe(&navigation.payload, 32, 7u, 4u);
  writeLe(&navigation.payload, 40, 1780000000000000ULL, 8u);
  writeDouble(&navigation.payload, 48, 31.2304);
  writeDouble(&navigation.payload, 56, 121.4737);
  writeDouble(&navigation.payload, 64, 12.5);
  writeDouble(&navigation.payload, 72, 0.021);
  writeDouble(&navigation.payload, 80, 0.013);
  writeDouble(&navigation.payload, 88, 0.025);
  writeDouble(&navigation.payload, 96, 0.4);
  writeDouble(&navigation.payload, 104, 4.018);
  writeDouble(&navigation.payload, 112, 0.006);
  writeLe(&navigation.payload, 120, 20u, 8u);
  writeLe(&navigation.payload, 128, 18u, 8u);
  writeLe(&navigation.payload, 136, 2u, 8u);
  writeLe(&navigation.payload, 144, 25u, 8u);
  writeLe(&navigation.payload, 152, 24u, 8u);
  writeLe(&navigation.payload, 160, 1u, 8u);
  const auto navigation_status = parseRtkNavigationStatus(navigation);
  ok &= require(navigation_status.solution_valid &&
                    navigation_status.confidence_valid &&
                    navigation_status.position_jump_valid &&
                    navigation_status.solution == RtkSolution::Fix &&
                    navigation_status.confidence == RtkConfidence::High &&
                    navigation_status.confidence_score == 932u &&
                    navigation_status.satellites == 17u &&
                    navigation_status.solution_epoch_us ==
                        1780000000000000LL &&
                    navigation_status.latitude_deg == 31.2304 &&
                    navigation_status.ambiguity_ratio == 4.018 &&
                    navigation_status.solution_count == 20u,
                "RTK navigation fields were not parsed");

  navigation.payload.at(20) = 0xe9u;
  navigation.payload.at(21) = 0x03u;
  rejected = false;
  try {
    (void)parseRtkNavigationStatus(navigation);
  } catch (const std::runtime_error&) {
    rejected = true;
  }
  ok &= require(rejected, "Out-of-range RTK confidence was accepted");
  return ok ? 0 : 1;
}
