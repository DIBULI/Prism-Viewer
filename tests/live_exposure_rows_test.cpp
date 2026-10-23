#include "communication/live_exposure_rows.hpp"
#include <iostream>
#include <stdexcept>

void require(bool value) { if (!value) throw std::runtime_error("live row regression"); }
struct Client {
  bool pending = false, bad = false;
  unsigned writes = 0;
  std::array<uint16_t,4> rows{};
  bool liveExposureRowsPending() const { return pending; }
  void beginLiveExposureRows(uint8_t mask, const std::array<uint16_t,4>& value) {
    require(mask == 15 && !pending); rows = value; pending = true; ++writes;
  }
  bool finishLiveExposureRows(const prism::Frame& frame, prism::ExposureConfiguration&) {
    if (frame.type != prism::FrameType::ExposureResponse || frame.sequence != 7) return false;
    if (bad) throw std::runtime_error("invalid response");
    pending = false; return true;
  }
};
int main() {
  using namespace std::chrono;
  using prism_viewer::LiveExposureRows;
  Client client;
  LiveExposureRows live;
  prism::ExposureConfiguration config, applied;
  config.automatic_camera_mask = 0;
  config.manual_exposure_time_us = {188, 64063, 128000, 256000};
  auto now = LiveExposureRows::Clock::time_point{};
  live.begin(client, config, now);
  require(client.rows == std::array<uint16_t,4>{3,1025,2048,4096} && client.writes == 1);
  for (auto type : {prism::FrameType::VideoChunk, prism::FrameType::VideoMeta,
                    prism::FrameType::ImuSample, prism::FrameType::Heartbeat}) {
    prism::Frame frame; frame.type=type; frame.sequence=7;
    require(!live.accept(client, frame, applied) && live.waiting());
  }
  prism::Frame response; response.type=prism::FrameType::ExposureResponse; response.sequence=8;
  require(!live.accept(client, response, applied));
  require(!live.expired(now+seconds(4)) && live.expired(now+seconds(5)));
  require(!live.expired(now+seconds(6)) && live.waiting() && client.writes==1);
  bool rejected=false;
  try { live.begin(client,config,now); } catch(...) { rejected=true; }
  require(rejected && client.writes==1);
  response.sequence=7;
  require(live.accept(client,response,applied) && !live.waiting());
  client.bad=true;
  live.begin(client,config,now);
  rejected=false;
  try { live.accept(client,response,applied); } catch(...) { rejected=true; }
  require(rejected && client.pending && !live.waiting());
  rejected=false;
  try { live.begin(client,config,now); } catch(...) { rejected=true; }
  require(rejected && client.writes==2);
  client.pending=false; client.bad=false;
  for (auto us : {187u, 64001u, 256063u, 0xffffffffu}) {
    config.manual_exposure_time_us[0]=us; rejected=false;
    try { live.begin(client,config,now); } catch(...) { rejected=true; }
    require(rejected && client.writes==2);
  }
  config.manual_exposure_time_us[0]=188;
  config.gain_x1024[0]=2048; rejected=false;
  try { live.begin(client,config,now); } catch(...) { rejected=true; }
  require(rejected && client.writes==2);
  std::cout << "PASS: live rows preserve all stream frames, bound confirmation, never retry\n";
}
