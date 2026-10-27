#include "cors/cors_session.hpp"
#include <QtCore/QCoreApplication>
#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>
#include <stdexcept>
using namespace prism_viewer::cors;
static void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void until(F ready) {
  for(int i=0;i<400 && !ready();++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
  check(ready(), "Timed out waiting for session");
}
int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  try {
    CorsConfiguration config;config.service_provider="qianxun";
    config.endpoints.push_back({"test", "203.0.113.1", 8002, false});
    config.mountpoint="AUTO";config.username="test-user";config.password="test-password";
    NativeCorsTransport transport;
    std::atomic<int> saves{0}, starts{0}, stops{0};
    std::atomic<bool> fresh{true}, old_generation{false};
    std::atomic<bool> authenticated{false}, fail_stop{false}, wrong_mode{false}, fail_start{false};
    transport.port=[&] { prism::TimeSyncPortStatus s; s.applied=true; s.mode=wrong_mode ? prism::TimeSyncPortMode::GnssInput : prism::TimeSyncPortMode::Rtk;return s; };
    transport.save=[&](const prism::TimeSyncCorsConfiguration& c) {
      check(c.ip=="203.0.113.1" && c.port==8002 && c.mountpoint=="AUTO" && c.username=="test-user" && c.password=="test-password" && c.enabled,"CORS fields changed");
      ++saves;prism::TimeSyncCorsStatus s;s.configuration_saved=true;s.enabled=true;s.credentials_present=true;s.saved_generation=17;return s;
    };
    transport.status=[&] { prism::TimeSyncRtkStatus s;s.linked=true;s.device_status_fresh=s.control_status_fresh=fresh;s.configuration_applied=true;s.saved_generation=17;s.current_cors_generation=old_generation?16:17;s.applied_generation=17;s.control_state=2;s.device_flags=authenticated?32:0;s.rtcm_frames=authenticated?3:0;s.network_bytes=240;s.transmitted_bytes=200;s.fix=5;return s; };
    transport.start=[&](const prism::RtkStartOptions& o) {
      check(o.allow_gga && o.expected_cors_generation==17,"GGA consent must bind saved generation");++starts;
      if(fail_start) throw std::runtime_error("Start timed out");
      return transport.status();
    };
    transport.stop=[&] { ++stops;if(fail_stop)throw std::runtime_error("Stop not confirmed");prism::TimeSyncRtkStatus s;s.linked=s.control_status_fresh=true;s.control_state=4;return s; };
    CorsSession session;std::mutex mutex;CorsSessionStatus latest;
    auto handler=[&](const CorsSessionStatus& s) {std::lock_guard<std::mutex> lock(mutex);latest=s;};
    auto snapshot=[&] {std::lock_guard<std::mutex> lock(mutex);return latest;};
    session.startNative(config,transport,handler);
    until([&] {return snapshot().phase==CorsSessionPhase::Connecting;});
    check(snapshot().phase!=CorsSessionPhase::Streaming,"Saved config is not a CORS connection");
    authenticated=true;
    until([&] {return snapshot().phase==CorsSessionPhase::Streaming;});
    check(snapshot().native_status.fix==5 && snapshot().received_bytes==240 && snapshot().forwarded_bytes==200,"Module telemetry missing");
    fresh=false;
    until([&] {return snapshot().phase==CorsSessionPhase::Connecting;});
    fresh=true;old_generation=true;
    until([&] {return snapshot().native_status.current_cors_generation==16;});
    check(snapshot().phase==CorsSessionPhase::Connecting,"Old generation must not appear connected");
    old_generation=false;
    until([&] {return snapshot().phase==CorsSessionPhase::Streaming;});
    session.stop();check(stops==1 && snapshot().phase==CorsSessionPhase::Disconnected,"Normal stop failed");
    wrong_mode=true;
    session.startNative(config,transport,handler);until([&] {return !session.active();});session.stop();
    check(saves==1 && starts==1 && stops==1 && snapshot().phase==CorsSessionPhase::Error,"Wrong port must not mutate config or start RTK");
    wrong_mode=false;fail_start=true;
    session.startNative(config,transport,handler);until([&] {return !session.active();});session.stop();
    check(stops==2 && snapshot().phase==CorsSessionPhase::Error,"Ambiguous start must attempt stop and preserve error");
    fail_start=false;fail_stop=true;
    session.startNative(config,transport,handler);until([&] {return snapshot().phase==CorsSessionPhase::Streaming;});session.stop();
    check(snapshot().phase==CorsSessionPhase::Error && snapshot().error.contains("Stop not confirmed"),"Do not report disconnected after failed stop");
    for (int kind=0;kind<2;++kind) {
      auto bad=config;if(kind)bad.endpoints[0].tls=true;else bad.endpoints[0].host="caster.example";
      bool rejected=false;try {session.startNative(bad,transport,handler);}catch(const std::invalid_argument&){rejected=true;}
      check(rejected,"Reject unsupported endpoint before starting worker");
    }
    // Changing the account must save the new values and bind START to that
    // generation. Authenticated telemetry for the prior account is not success.
    fail_stop=false;
    auto revised=config;
    revised.username="replacement-user";revised.password="replacement-password";
    revised.mountpoint="SECOND";
    NativeCorsTransport replacement=transport;
    std::atomic<bool> replacement_saved{false};
    std::atomic<uint32_t> active_generation{17};
    replacement.save=[&](const prism::TimeSyncCorsConfiguration& c) {
      check(c.enabled && c.ip=="203.0.113.1" && c.port==8002 &&
            c.username=="replacement-user" && c.password=="replacement-password" &&
            c.mountpoint=="SECOND","Replacement account fields were not saved");
      replacement_saved=true;
      prism::TimeSyncCorsStatus s;s.configuration_saved=s.enabled=s.credentials_present=true;
      s.saved_generation=18;return s;
    };
    replacement.status=[&] {
      auto s=transport.status();
      s.saved_generation=18;s.applied_generation=s.current_cors_generation=active_generation.load();
      return s;
    };
    replacement.start=[&](const prism::RtkStartOptions& options) {
      check(replacement_saved && options.expected_cors_generation==18 && options.allow_gga,
            "Replacement account must be saved before START with its generation");
      return replacement.status();
    };
    session.startNative(revised,replacement,handler);
    until([&] {return replacement_saved && snapshot().phase==CorsSessionPhase::Connecting;});
    check(snapshot().native_status.current_cors_generation==17,
          "Old-account fixture was not delivered");
    active_generation=18;
    until([&] {return snapshot().phase==CorsSessionPhase::Streaming;});
    check(snapshot().native_status.current_cors_generation==18,"Replacement generation missing");
    session.stop();check(snapshot().phase==CorsSessionPhase::Disconnected,"Replacement stop failed");
  } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
  return 0;
}
