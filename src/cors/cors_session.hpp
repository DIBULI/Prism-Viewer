#pragma once

#include "communication/rtk_corrections.hpp"
#include "cors/cors_config.hpp"
#include "prism/usb/timesync_port.hpp"

#include <QtCore/QString>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>

namespace prism_viewer::cors {

enum class CorsSessionPhase {
  Disconnected,
  Starting,
  Connecting,
  Streaming,
  Reconnecting,
  Stopping,
  Error,
};

struct CorsSessionStatus {
  CorsSessionPhase phase = CorsSessionPhase::Disconnected;
  QString endpoint;
  QString error;
  uint64_t received_bytes = 0;
  uint64_t forwarded_bytes = 0;
  bool native_status_valid = false;
  prism::TimeSyncRtkStatus native_status;
  bool rtk_status_valid = false;
  communication::RtkCorrectionStatus rtk_status;
};

struct CorsCorrectionTransport {
  std::function<communication::RtkCorrectionStatus()> begin;
  std::function<communication::RtkCorrectionStatus(
      const uint8_t*, size_t)> send;
  std::function<communication::RtkCorrectionStatus()> end;
};

struct NativeCorsTransport {
  std::function<prism::TimeSyncPortStatus()> port;
  std::function<prism::TimeSyncCorsStatus(const prism::TimeSyncCorsConfiguration&)> save;
  std::function<prism::TimeSyncRtkStatus(const prism::RtkStartOptions&)> start;
  std::function<prism::TimeSyncRtkStatus()> status;
  std::function<prism::TimeSyncRtkStatus()> stop;
};

class CorsSession final {
 public:
  using StatusHandler = std::function<void(const CorsSessionStatus&)>;

  CorsSession() = default;
  ~CorsSession();
  CorsSession(const CorsSession&) = delete;
  CorsSession& operator=(const CorsSession&) = delete;

  void start(const CorsConfiguration& configuration,
             CorsCorrectionTransport transport,
             StatusHandler status_handler);
  void startNative(const CorsConfiguration& configuration, NativeCorsTransport transport,
                   StatusHandler status_handler);
  void setLiveGga(const std::optional<CorsGgaData>& data);
  void requestStop() noexcept;
  void stop();
  bool active() const noexcept;

 private:
  void workerMain(CorsConfiguration configuration,
                  CorsCorrectionTransport transport,
                  StatusHandler status_handler);
  QByteArray currentGga(const CorsConfiguration& configuration) const;

  std::atomic<bool> stop_requested_{false};
  std::atomic<bool> active_{false};
  mutable std::mutex live_gga_mutex_;
  std::optional<CorsGgaData> live_gga_;
  std::chrono::steady_clock::time_point live_gga_updated_at_{};
  std::thread worker_;
};

QString corsSessionPhaseName(CorsSessionPhase phase);

}  // namespace prism_viewer::cors
