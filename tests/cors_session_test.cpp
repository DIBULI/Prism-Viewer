#include "cors/cors_session.hpp"

#include <QtCore/QCoreApplication>
#include <QtCore/QElapsedTimer>
#include <QtCore/QThread>
#include <QtNetwork/QHostAddress>
#include <QtNetwork/QTcpServer>
#include <QtNetwork/QTcpSocket>

#include <atomic>
#include <cstdint>
#include <iostream>
#include <utility>

namespace {

bool require(bool condition, const char* message) {
  if (!condition) std::cerr << message << '\n';
  return condition;
}

QByteArray readUntil(QTcpSocket* socket, const QByteArray& marker,
                     int timeout_ms) {
  QByteArray bytes;
  QElapsedTimer timer;
  timer.start();
  while (!bytes.contains(marker) && timer.elapsed() < timeout_ms) {
    if (socket->bytesAvailable() == 0) socket->waitForReadyRead(50);
    bytes += socket->readAll();
  }
  return bytes;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication application(argc, argv);
  bool ok = true;

  QTcpServer caster;
  ok &= require(caster.listen(QHostAddress::LocalHost, 0),
                "Failed to start local NTRIP caster");
  if (!ok) return 1;

  prism_viewer::cors::CorsConfiguration configuration;
  configuration.service_provider = QStringLiteral("china_mobile");
  configuration.endpoints.push_back(
      {QStringLiteral("test"), QStringLiteral("127.0.0.1"),
       caster.serverPort(), false});
  configuration.mountpoint = QStringLiteral("RTCM33_GRCEJ");
  configuration.username = QStringLiteral("test-user");
  configuration.password = QStringLiteral("test-password");
  configuration.latitude_degrees = 31.2304;
  configuration.longitude_degrees = 121.4737;
  configuration.altitude_meters = 12.5;

  std::atomic<uint64_t> forwarded_bytes{0};
  prism_viewer::cors::CorsCorrectionTransport transport;
  transport.begin = []() {
    return prism_viewer::communication::RtkCorrectionStatus{};
  };
  transport.send = [&forwarded_bytes](const uint8_t*, size_t size) {
    forwarded_bytes.fetch_add(size, std::memory_order_relaxed);
    return prism_viewer::communication::RtkCorrectionStatus{};
  };
  transport.end = []() {
    return prism_viewer::communication::RtkCorrectionStatus{};
  };

  prism_viewer::cors::CorsSession session;
  prism_viewer::cors::CorsGgaData first_live_gga;
  first_live_gga.utc = QTime(12, 34, 56, 700);
  first_live_gga.latitude_degrees = 31.1394098;
  first_live_gga.longitude_degrees = 121.5650333;
  first_live_gga.altitude_meters = 14.858;
  first_live_gga.geoid_separation_meters = 12.160;
  first_live_gga.fix_quality = 4;
  first_live_gga.satellites = 14;
  first_live_gga.hdop = 1.3;
  session.setLiveGga(first_live_gga);
  session.start(configuration, std::move(transport),
                [](const prism_viewer::cors::CorsSessionStatus&) {});

  ok &= require(caster.waitForNewConnection(3000),
                "Viewer did not connect to the local NTRIP caster");
  QTcpSocket* peer = caster.nextPendingConnection();
  if (peer == nullptr) {
    session.stop();
    return 1;
  }

  const QByteArray request = readUntil(peer, QByteArray("\r\n\r\n"), 3000);
  ok &= require(request.contains(
                    "Ntrip-GGA: $GPGGA,123456.70,3108.36459,N,"
                    "12133.90200,E,4,14,1.30,14.858,M,12.160,M,,"),
                "Initial NTRIP request did not use live device GNSS");

  peer->write("ICY 200 OK\r\n");
  peer->waitForBytesWritten(1000);
  const QByteArray standalone_gga =
      readUntil(peer, QByteArray("\r\n"), 3000);
  ok &= require(standalone_gga.startsWith("$GPGGA,") &&
                    standalone_gga.endsWith("\r\n"),
                "Viewer did not send standalone GGA after bare ICY response");

  prism_viewer::cors::CorsGgaData updated_live_gga = first_live_gga;
  updated_live_gga.utc = QTime(12, 34, 57, 800);
  updated_live_gga.latitude_degrees = 31.1400000;
  session.setLiveGga(updated_live_gga);
  const QByteArray periodic_gga =
      readUntil(peer, QByteArray("\r\n"), 3000);
  ok &= require(periodic_gga.startsWith(
                    "$GPGGA,123457.80,3108.40000,N,"),
                "Periodic one-second GGA did not use updated device GNSS");

  QByteArray correction;
  correction.append(char(0xd3));
  correction.append(char(0x00));
  correction.append(char(0x00));
  peer->write(correction);
  peer->waitForBytesWritten(1000);

  QElapsedTimer forwarded_timer;
  forwarded_timer.start();
  while (forwarded_bytes.load(std::memory_order_relaxed) <
             static_cast<uint64_t>(correction.size()) &&
         forwarded_timer.elapsed() < 3000) {
    QThread::msleep(10);
  }
  ok &= require(forwarded_bytes.load(std::memory_order_relaxed) ==
                    static_cast<uint64_t>(correction.size()),
                "RTCM following bare ICY response was not forwarded");

  session.stop();
  delete peer;
  return ok ? 0 : 1;
}
