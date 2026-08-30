#include "cors/cors_session.hpp"

#include <QtCore/QDateTime>
#include <QtCore/QElapsedTimer>
#include <QtNetwork/QAbstractSocket>
#include <QtNetwork/QTcpSocket>

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <utility>

namespace prism_viewer::cors {
namespace {

constexpr int kConnectTimeoutMs = 5000;
constexpr int kHeaderTimeoutMs = 5000;
constexpr int kSocketPollMs = 100;
constexpr int kCorrectionFlushMs = 250;
constexpr int kGgaPeriodMs = 5000;
constexpr int kReconnectDelayMs = 2000;
constexpr int kMaximumCorrectionBatchBytes = 16 * 1024;

class FatalSessionError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

bool waitInterruptibly(const std::atomic<bool>& stop_requested,
                       int milliseconds) {
  int remaining = milliseconds;
  while (!stop_requested.load(std::memory_order_acquire) && remaining > 0) {
    const int slice = std::min(remaining, 100);
    std::this_thread::sleep_for(std::chrono::milliseconds(slice));
    remaining -= slice;
  }
  return !stop_requested.load(std::memory_order_acquire);
}

void writeAll(QTcpSocket* socket, const QByteArray& bytes,
              const std::atomic<bool>& stop_requested) {
  qint64 offset = 0;
  while (offset < bytes.size() &&
         !stop_requested.load(std::memory_order_acquire)) {
    const qint64 written =
        socket->write(bytes.constData() + offset, bytes.size() - offset);
    if (written < 0) {
      throw std::runtime_error(socket->errorString().toStdString());
    }
    offset += written;
    if (socket->bytesToWrite() > 0 && !socket->waitForBytesWritten(1000) &&
        socket->error() != QAbstractSocket::UnknownSocketError) {
      throw std::runtime_error(socket->errorString().toStdString());
    }
  }
  if (stop_requested.load(std::memory_order_acquire)) {
    throw std::runtime_error("CORS session stopped");
  }
}

QString endpointLabel(const CorsEndpoint& endpoint) {
  return QStringLiteral("%1 (%2:%3)")
      .arg(endpoint.name, endpoint.host)
      .arg(endpoint.port);
}

}  // namespace

CorsSession::~CorsSession() { stop(); }

void CorsSession::start(const CorsConfiguration& configuration,
                        CorsCorrectionTransport transport,
                        StatusHandler status_handler) {
  stop();
  const QString validation = validateCorsConfiguration(configuration);
  if (!validation.isEmpty()) {
    throw std::invalid_argument(validation.toStdString());
  }
  if (!transport.begin || !transport.send || !transport.end) {
    throw std::invalid_argument("CORS correction transport is incomplete");
  }
  stop_requested_.store(false, std::memory_order_release);
  active_.store(true, std::memory_order_release);
  worker_ = std::thread(
      [this, configuration, transport = std::move(transport),
       status_handler = std::move(status_handler)]() mutable {
        workerMain(configuration, std::move(transport),
                   std::move(status_handler));
      });
}

void CorsSession::requestStop() noexcept {
  stop_requested_.store(true, std::memory_order_release);
}

void CorsSession::stop() {
  requestStop();
  if (worker_.joinable()) worker_.join();
  active_.store(false, std::memory_order_release);
}

bool CorsSession::active() const noexcept {
  return active_.load(std::memory_order_acquire);
}

void CorsSession::workerMain(CorsConfiguration configuration,
                             CorsCorrectionTransport transport,
                             StatusHandler status_handler) {
  CorsSessionStatus status;
  status.phase = CorsSessionPhase::Starting;
  const auto publish = [&]() {
    if (status_handler) status_handler(status);
  };
  publish();

  bool correction_source_acquired = false;
  bool fatal_error = false;
  try {
    size_t endpoint_index = 0u;
    while (!stop_requested_.load(std::memory_order_acquire)) {
      const CorsEndpoint endpoint = configuration.endpoints.at(
          static_cast<int>(endpoint_index %
                           static_cast<size_t>(configuration.endpoints.size())));
      ++endpoint_index;
      status.endpoint = endpointLabel(endpoint);
      status.error.clear();
      status.phase =
          endpoint_index <= static_cast<size_t>(configuration.endpoints.size())
              ? CorsSessionPhase::Connecting
              : CorsSessionPhase::Reconnecting;
      publish();

      QTcpSocket socket;
      socket.connectToHost(endpoint.host, endpoint.port);
      QElapsedTimer connect_timer;
      connect_timer.start();
      while (!stop_requested_.load(std::memory_order_acquire) &&
             socket.state() == QAbstractSocket::ConnectingState &&
             connect_timer.elapsed() < kConnectTimeoutMs) {
        socket.waitForConnected(kSocketPollMs);
      }
      if (stop_requested_.load(std::memory_order_acquire)) break;
      if (socket.state() != QAbstractSocket::ConnectedState) {
        status.error = socket.errorString();
        status.phase = CorsSessionPhase::Reconnecting;
        publish();
        if (endpoint_index %
                    static_cast<size_t>(configuration.endpoints.size()) ==
                0u &&
            !waitInterruptibly(stop_requested_, kReconnectDelayMs)) {
          break;
        }
        continue;
      }

      const QByteArray initial_gga =
          buildNmeaGga(QDateTime::currentDateTimeUtc(),
                       configuration.latitude_degrees,
                       configuration.longitude_degrees,
                       configuration.altitude_meters);
      writeAll(&socket,
               buildNtripRequest(configuration, endpoint, initial_gga),
               stop_requested_);

      QByteArray response;
      QElapsedTimer header_timer;
      header_timer.start();
      NtripResponseInspection inspection;
      while (!stop_requested_.load(std::memory_order_acquire) &&
             header_timer.elapsed() < kHeaderTimeoutMs) {
        if (socket.bytesAvailable() == 0) {
          socket.waitForReadyRead(kSocketPollMs);
        }
        response += socket.readAll();
        inspection = inspectNtripResponse(response);
        if (inspection.complete) break;
        if (socket.state() == QAbstractSocket::UnconnectedState) break;
      }
      if (stop_requested_.load(std::memory_order_acquire)) break;
      if (!inspection.complete) {
        status.error = QStringLiteral("Timed out waiting for NTRIP headers");
        status.phase = CorsSessionPhase::Reconnecting;
        publish();
        continue;
      }
      if (!inspection.accepted) {
        throw FatalSessionError(inspection.error.toStdString());
      }

      status.rtk_status = transport.begin();
      status.rtk_status_valid = true;
      correction_source_acquired = true;
      status.phase = CorsSessionPhase::Streaming;
      status.error.clear();

      QByteArray pending = response.mid(inspection.body_offset);
      status.received_bytes += static_cast<uint64_t>(pending.size());
      publish();
      QElapsedTimer flush_timer;
      flush_timer.start();
      QElapsedTimer gga_timer;
      gga_timer.start();
      QElapsedTimer status_timer;
      status_timer.start();
      bool network_disconnected = false;
      while (!stop_requested_.load(std::memory_order_acquire)) {
        if (socket.bytesAvailable() == 0) {
          socket.waitForReadyRead(kSocketPollMs);
        }
        const QByteArray received = socket.readAll();
        if (!received.isEmpty()) {
          pending += received;
          status.received_bytes += static_cast<uint64_t>(received.size());
        }

        const bool socket_closed =
            socket.state() == QAbstractSocket::UnconnectedState &&
            socket.bytesAvailable() == 0;
        while (!pending.isEmpty() &&
               (pending.size() >= kMaximumCorrectionBatchBytes ||
                flush_timer.elapsed() >= kCorrectionFlushMs ||
                socket_closed)) {
          const int chunk_size =
              std::min(pending.size(), kMaximumCorrectionBatchBytes);
          const QByteArray chunk = pending.left(chunk_size);
          pending.remove(0, chunk_size);
          status.rtk_status = transport.send(
              reinterpret_cast<const uint8_t*>(chunk.constData()),
              static_cast<size_t>(chunk.size()));
          status.rtk_status_valid = true;
          status.forwarded_bytes += static_cast<uint64_t>(chunk.size());
          flush_timer.restart();
        }

        if (gga_timer.elapsed() >= kGgaPeriodMs) {
          const QByteArray gga =
              buildNmeaGga(QDateTime::currentDateTimeUtc(),
                           configuration.latitude_degrees,
                           configuration.longitude_degrees,
                           configuration.altitude_meters);
          writeAll(&socket, gga + QByteArray("\r\n"), stop_requested_);
          gga_timer.restart();
        }
        if (status_timer.elapsed() >= 1000) {
          publish();
          status_timer.restart();
        }
        if (socket_closed) {
          network_disconnected = true;
          break;
        }
      }

      if (correction_source_acquired) {
        try {
          status.rtk_status = transport.end();
          status.rtk_status_valid = true;
        } catch (...) {
        }
        correction_source_acquired = false;
      }
      if (stop_requested_.load(std::memory_order_acquire)) break;
      if (network_disconnected) {
        status.error = socket.errorString();
        if (status.error.isEmpty()) {
          status.error = QStringLiteral("NTRIP connection closed");
        }
        status.phase = CorsSessionPhase::Reconnecting;
        publish();
      }
      if (endpoint_index %
                  static_cast<size_t>(configuration.endpoints.size()) ==
              0u &&
          !waitInterruptibly(stop_requested_, kReconnectDelayMs)) {
        break;
      }
    }
  } catch (const FatalSessionError& error) {
    fatal_error = true;
    status.error = QString::fromUtf8(error.what());
  } catch (const std::exception& error) {
    if (!stop_requested_.load(std::memory_order_acquire)) {
      fatal_error = true;
      status.error = QString::fromUtf8(error.what());
    }
  }

  if (correction_source_acquired) {
    try {
      status.rtk_status = transport.end();
      status.rtk_status_valid = true;
    } catch (...) {
    }
  }
  status.phase = fatal_error ? CorsSessionPhase::Error
                             : CorsSessionPhase::Disconnected;
  publish();
  active_.store(false, std::memory_order_release);
}

QString corsSessionPhaseName(CorsSessionPhase phase) {
  switch (phase) {
    case CorsSessionPhase::Disconnected: return QStringLiteral("disconnected");
    case CorsSessionPhase::Starting: return QStringLiteral("starting");
    case CorsSessionPhase::Connecting: return QStringLiteral("connecting");
    case CorsSessionPhase::Streaming: return QStringLiteral("streaming");
    case CorsSessionPhase::Reconnecting: return QStringLiteral("reconnecting");
    case CorsSessionPhase::Stopping: return QStringLiteral("stopping");
    case CorsSessionPhase::Error: return QStringLiteral("error");
  }
  return QStringLiteral("unknown");
}

}  // namespace prism_viewer::cors
