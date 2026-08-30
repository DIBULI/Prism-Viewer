#include "communication/rtk_corrections.hpp"

#include <iostream>
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
  return ok ? 0 : 1;
}
