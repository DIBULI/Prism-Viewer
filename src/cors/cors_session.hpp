#pragma once

#include "communication/rtk_corrections.hpp"
#include "cors/cors_config.hpp"

#include <QtCore/QString>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
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
  bool rtk_status_valid = false;
  communication::RtkCorrectionStatus rtk_status;
};

struct CorsCorrectionTransport {
  std::function<communication::RtkCorrectionStatus()> begin;
  std::function<communication::RtkCorrectionStatus(
      const uint8_t*, size_t)> send;
  std::function<communication::RtkCorrectionStatus()> end;
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
  void requestStop() noexcept;
  void stop();
  bool active() const noexcept;

 private:
  void workerMain(CorsConfiguration configuration,
                  CorsCorrectionTransport transport,
                  StatusHandler status_handler);

  std::atomic<bool> stop_requested_{false};
  std::atomic<bool> active_{false};
  std::thread worker_;
};

QString corsSessionPhaseName(CorsSessionPhase phase);

}  // namespace prism_viewer::cors
