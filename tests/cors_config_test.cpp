#include "cors/cors_config.hpp"

#include <QtCore/QDate>
#include <QtCore/QTime>

#include <iostream>

namespace {
bool require(bool condition, const char* message) {
  if (!condition) std::cerr << message << '\n';
  return condition;
}
quint8 nmeaChecksum(const QByteArray& sentence) {
  quint8 value = 0;
  for (char byte : sentence) value ^= static_cast<quint8>(byte);
  return value;
}
}  // namespace

int main() {
  using namespace prism_viewer::cors;
  bool ok = true;
  const auto* provider =
      findCorsServiceProvider(QStringLiteral("china_mobile"));
  ok &= require(provider != nullptr, "China Mobile provider is missing");
  if (provider == nullptr) return 1;
  ok &= require(provider->endpoints.size() == 2,
                "China Mobile primary/backup endpoints are missing");
  ok &= require(provider->mountpoints.size() == 5,
                "China Mobile mountpoint catalog is incomplete");

  auto parsed = parseCorsEndpointAddress(
      QStringLiteral("120.253.226.97"), 8002);
  ok &= require(parsed.valid() &&
                    parsed.endpoint.host == QStringLiteral("120.253.226.97") &&
                    parsed.endpoint.port == 8002 && !parsed.endpoint.tls,
                "Manual IPv4 caster address was not parsed");
  parsed = parseCorsEndpointAddress(
      QStringLiteral("caster.example.com:2101"), 8002);
  ok &= require(parsed.valid() &&
                    parsed.endpoint.host ==
                        QStringLiteral("caster.example.com") &&
                    parsed.endpoint.port == 2101,
                "Manual hostname and port were not parsed");
  parsed = parseCorsEndpointAddress(
      QStringLiteral("https://cors.example.com/RTCM33_GRCEJ"), 8002);
  ok &= require(parsed.valid() && parsed.endpoint.tls &&
                    parsed.endpoint.port == 443 &&
                    parsed.mountpoint == QStringLiteral("RTCM33_GRCEJ"),
                "HTTPS caster URL was not parsed");
  parsed = parseCorsEndpointAddress(
      QStringLiteral("http://[2001:db8::1]:8001/RTCM30_GR"), 8002);
  ok &= require(parsed.valid() &&
                    parsed.endpoint.host == QStringLiteral("2001:db8::1") &&
                    parsed.endpoint.port == 8001 &&
                    parsed.mountpoint == QStringLiteral("RTCM30_GR"),
                "IPv6 caster URL was not parsed");
  ok &= require(
      !parseCorsEndpointAddress(QStringLiteral("ftp://bad.example.com"), 8002)
           .valid(),
      "Unsupported caster URL scheme was accepted");
  ok &= require(
      !parseCorsEndpointAddress(QStringLiteral(""), 8002).valid(),
      "Empty caster address was accepted");

  CorsConfiguration configuration;
  configuration.service_provider = provider->id;
  configuration.endpoints = provider->endpoints;
  configuration.mountpoint = QStringLiteral("RTCM33_GRCEJ");
  configuration.username = QStringLiteral("test-user");
  configuration.password = QStringLiteral("test-password");
  configuration.latitude_degrees = 31.2304;
  configuration.longitude_degrees = 121.4737;
  configuration.altitude_meters = 12.5;
  ok &= require(validateCorsConfiguration(configuration).isEmpty(),
                "Valid China Mobile configuration was rejected");

  const QDateTime time(QDate(2026, 8, 31), QTime(12, 34, 56, 0), Qt::UTC);
  const QByteArray gga =
      buildNmeaGga(time, configuration.latitude_degrees,
                   configuration.longitude_degrees,
                   configuration.altitude_meters);
  ok &= require(gga.startsWith("$GPGGA,123456.00,3113.82400,N,"
                               "12128.42200,E,1,12,1.00,12.500,M"),
                "Generated GGA fields are incorrect");
  const int star = gga.indexOf('*');
  ok &= require(star > 1, "Generated GGA checksum delimiter is missing");
  if (star > 1) {
    const quint8 expected = nmeaChecksum(gga.mid(1, star - 1));
    ok &= require(gga.mid(star + 1).toUInt(nullptr, 16) == expected,
                  "Generated GGA checksum is incorrect");
  }

  prism::GnssTimingStatus timing;
  timing.nmea_seen = true;
  timing.nmea_fix_valid = true;
  timing.nmea_dop_valid = true;
  timing.nmea_position_valid = true;
  timing.nmea_fix_quality = 4;
  timing.satellites = 14;
  timing.hdop_milli = 1300;
  timing.nmea_age_ms = 100;
  timing.latitude_e7 = 311394098;
  timing.longitude_e7 = 1215650333;
  timing.altitude_mm = 14858;
  timing.geoid_separation_mm = 12160;
  timing.utc_ms_of_day = 18u * 3600000u + 39u * 60000u + 55400u;
  const auto live_gga_data = corsGgaDataFromGnssStatus(timing);
  ok &= require(live_gga_data.has_value(),
                "Fresh device GNSS status was rejected for CORS GGA");
  if (live_gga_data.has_value()) {
    const QByteArray live_gga = buildNmeaGga(*live_gga_data);
    ok &= require(
        live_gga.startsWith("$GPGGA,183955.40,3108.36459,N,"
                            "12133.90200,E,4,14,1.30,14.858,M,12.160,M,,"),
        "Device GNSS fields were not preserved in CORS GGA");
  }
  timing.nmea_age_ms = 2001;
  ok &= require(!corsGgaDataFromGnssStatus(timing).has_value(),
                "Stale device GNSS status was accepted for CORS GGA");

  CorsEndpoint endpoint = provider->endpoints.front();
  endpoint.port = 8002;
  const QByteArray request =
      buildNtripRequest(configuration, endpoint, gga);
  ok &= require(request.startsWith("GET /RTCM33_GRCEJ HTTP/1.0\r\n"),
                "NTRIP request path/version is incorrect");
  ok &= require(request.contains("Authorization: Basic "),
                "NTRIP Basic authorization is missing");
  ok &= require(request.contains("Ntrip-GGA: $GPGGA,"),
                "NTRIP GGA header is missing");

  QByteArray response("ICY 200 OK\r\nServer: test\r\n\r\n");
  response.append(char(0xd3));
  response.append(char(0x00));
  response.append(char(0x00));
  const auto accepted = inspectNtripResponse(response);
  ok &= require(accepted.complete && accepted.accepted &&
                    accepted.body_offset ==
                        response.indexOf("\r\n\r\n") + 4,
                "Valid ICY response was not accepted");
  const QByteArray bare_icy("ICY 200 OK\r\n");
  const auto bare_icy_accepted = inspectNtripResponse(bare_icy);
  ok &= require(bare_icy_accepted.complete && bare_icy_accepted.accepted &&
                    bare_icy_accepted.body_offset == bare_icy.size(),
                "Bare NTRIP v1 ICY response was not accepted");
  const auto rejected =
      inspectNtripResponse(QByteArray("HTTP/1.1 401 Unauthorized\r\n\r\n"));
  ok &= require(rejected.complete && !rejected.accepted &&
                    rejected.error.contains(QStringLiteral("401")),
                "Authentication rejection was not reported");
  return ok ? 0 : 1;
}
