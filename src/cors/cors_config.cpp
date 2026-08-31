#include "cors/cors_config.hpp"

#include <QtCore/QLocale>
#include <QtCore/QUrl>

#include <cmath>

namespace prism_viewer::cors {
namespace {

constexpr int kMaximumNtripHeaderBytes = 16 * 1024;

QString coordinateField(double degrees, bool latitude) {
  const double absolute = std::abs(degrees);
  const int whole_degrees = static_cast<int>(std::floor(absolute));
  const double minutes = (absolute - whole_degrees) * 60.0;
  return QStringLiteral("%1%2")
      .arg(whole_degrees, latitude ? 2 : 3, 10, QLatin1Char('0'))
      .arg(minutes, 8, 'f', 5, QLatin1Char('0'));
}

QByteArray withNmeaChecksum(const QByteArray& sentence) {
  quint8 checksum = 0;
  for (char byte : sentence) checksum ^= static_cast<quint8>(byte);
  return QByteArray("$") + sentence + "*" +
         QByteArray::number(checksum, 16).rightJustified(2, '0').toUpper();
}

}  // namespace

const QVector<CorsServiceProvider>& corsServiceProviders() {
  static const QVector<CorsServiceProvider> providers{
      {
          QStringLiteral("china_mobile"),
          QStringLiteral("中国移动 CORS"),
          {
              {QStringLiteral("主服务"), QStringLiteral("120.253.226.97"), 8002},
              {QStringLiteral("备用服务"), QStringLiteral("120.253.239.161"), 8002},
          },
          {
              {QStringLiteral("RTCM33_GRCEJ"),
               QStringLiteral("RTCM33_GRCEJ（五星十六频）")},
              {QStringLiteral("RTCM33_GRCEpro"),
               QStringLiteral("RTCM33_GRCEpro（四星十三频）")},
              {QStringLiteral("RTCM33_GRCE"),
               QStringLiteral("RTCM33_GRCE（四星十一频）")},
              {QStringLiteral("RTCM33_GRC"),
               QStringLiteral("RTCM33_GRC（三星八频）")},
              {QStringLiteral("RTCM30_GR"),
               QStringLiteral("RTCM30_GR（双星）")},
          },
      },
  };
  return providers;
}

const CorsServiceProvider* findCorsServiceProvider(const QString& id) {
  for (const auto& provider : corsServiceProviders()) {
    if (provider.id == id) return &provider;
  }
  return nullptr;
}

CorsEndpointAddress parseCorsEndpointAddress(
    const QString& address, quint16 default_port) {
  CorsEndpointAddress result;
  const QString trimmed = address.trimmed();
  if (trimmed.isEmpty()) {
    result.error = QStringLiteral("Caster address is required");
    return result;
  }

  const bool explicit_scheme = trimmed.contains(QStringLiteral("://"));
  QString normalized = trimmed;
  if (!explicit_scheme) {
    // A bare IPv6 literal needs brackets before QUrl can distinguish it from
    // a host:port pair. Bracketed IPv6 and ordinary hostnames pass through.
    if (trimmed.count(QLatin1Char(':')) >= 2 &&
        !trimmed.startsWith(QLatin1Char('['))) {
      normalized = QStringLiteral("ntrip://[%1]").arg(trimmed);
    } else {
      normalized = QStringLiteral("ntrip://") + trimmed;
    }
  }

  const QUrl url(normalized, QUrl::StrictMode);
  const QString scheme = url.scheme().toLower();
  if (!url.isValid() || url.host().isEmpty()) {
    result.error =
        QStringLiteral("Invalid caster IP address, hostname or URL: %1")
            .arg(trimmed);
    return result;
  }
  if (scheme != QStringLiteral("http") &&
      scheme != QStringLiteral("https") &&
      scheme != QStringLiteral("ntrip") &&
      scheme != QStringLiteral("ntrips")) {
    result.error =
        QStringLiteral("Unsupported caster URL scheme: %1").arg(scheme);
    return result;
  }
  if (!url.userName().isEmpty() || !url.password().isEmpty()) {
    result.error = QStringLiteral(
        "Enter CORS credentials in the username and password fields");
    return result;
  }
  if (url.hasQuery() || url.hasFragment()) {
    result.error =
        QStringLiteral("Caster URL must not contain a query or fragment");
    return result;
  }

  const bool tls = scheme == QStringLiteral("https") ||
                   scheme == QStringLiteral("ntrips");
  const int fallback_port =
      explicit_scheme && tls ? 443 : static_cast<int>(default_port);
  const int port = url.port(fallback_port);
  if (port <= 0 || port > 65535) {
    result.error =
        QStringLiteral("Caster port must be between 1 and 65535");
    return result;
  }

  QString mountpoint = url.path(QUrl::FullyDecoded).trimmed();
  while (mountpoint.startsWith(QLatin1Char('/'))) mountpoint.remove(0, 1);
  while (mountpoint.endsWith(QLatin1Char('/'))) mountpoint.chop(1);
  if (mountpoint.contains(QLatin1Char('/'))) {
    result.error =
        QStringLiteral("Caster URL path must contain one mountpoint");
    return result;
  }

  result.endpoint.name = QStringLiteral("Manual");
  result.endpoint.host = url.host(QUrl::FullyDecoded);
  result.endpoint.port = static_cast<quint16>(port);
  result.endpoint.tls = tls;
  result.mountpoint = mountpoint;
  return result;
}

QString validateCorsConfiguration(
    const CorsConfiguration& configuration) {
  if (findCorsServiceProvider(configuration.service_provider) == nullptr) {
    return QStringLiteral("Unsupported CORS serviceProvider: %1")
        .arg(configuration.service_provider);
  }
  if (configuration.endpoints.isEmpty()) {
    return QStringLiteral("No CORS endpoint is configured");
  }
  for (const auto& endpoint : configuration.endpoints) {
    if (endpoint.host.trimmed().isEmpty() || endpoint.port == 0) {
      return QStringLiteral("CORS endpoint host or port is invalid");
    }
  }
  if (configuration.mountpoint.trimmed().isEmpty()) {
    return QStringLiteral("CORS mountpoint is required");
  }
  if (configuration.username.isEmpty()) {
    return QStringLiteral("CORS username is required");
  }
  if (configuration.password.isEmpty()) {
    return QStringLiteral("CORS password is required");
  }
  if (!std::isfinite(configuration.latitude_degrees) ||
      configuration.latitude_degrees < -90.0 ||
      configuration.latitude_degrees > 90.0) {
    return QStringLiteral("Latitude must be between -90 and 90 degrees");
  }
  if (!std::isfinite(configuration.longitude_degrees) ||
      configuration.longitude_degrees < -180.0 ||
      configuration.longitude_degrees > 180.0) {
    return QStringLiteral("Longitude must be between -180 and 180 degrees");
  }
  if (!std::isfinite(configuration.altitude_meters) ||
      configuration.altitude_meters < -1000.0 ||
      configuration.altitude_meters > 20000.0) {
    return QStringLiteral("Altitude must be between -1000 and 20000 meters");
  }
  if (std::abs(configuration.latitude_degrees) < 1e-9 &&
      std::abs(configuration.longitude_degrees) < 1e-9) {
    return QStringLiteral(
        "Enter an approximate rover latitude and longitude for NTRIP GGA");
  }
  return {};
}

QByteArray buildNmeaGga(const QDateTime& utc, double latitude_degrees,
                        double longitude_degrees, double altitude_meters) {
  const QTime utc_time = utc.toUTC().time();
  const QString time =
      QStringLiteral("%1.%2")
          .arg(utc_time.toString(QStringLiteral("hhmmss")))
          .arg(utc_time.msec() / 10, 2, 10, QLatin1Char('0'));
  const QChar latitude_hemisphere =
      latitude_degrees < 0.0 ? QLatin1Char('S') : QLatin1Char('N');
  const QChar longitude_hemisphere =
      longitude_degrees < 0.0 ? QLatin1Char('W') : QLatin1Char('E');
  const QString fields =
      QStringLiteral("GPGGA,%1,%2,%3,%4,%5,1,12,1.0,%6,M,0.0,M,,")
          .arg(time, coordinateField(latitude_degrees, true),
               QString(latitude_hemisphere),
               coordinateField(longitude_degrees, false),
               QString(longitude_hemisphere),
               QLocale::c().toString(altitude_meters, 'f', 2));
  return withNmeaChecksum(fields.toLatin1());
}

QByteArray buildNtripRequest(const CorsConfiguration& configuration,
                             const CorsEndpoint& endpoint,
                             const QByteArray& gga) {
  QString mountpoint = configuration.mountpoint.trimmed();
  while (mountpoint.startsWith(QLatin1Char('/'))) mountpoint.remove(0, 1);
  const QByteArray credentials =
      (configuration.username + QLatin1Char(':') + configuration.password)
          .toUtf8()
          .toBase64();
  QString host_header = endpoint.host;
  if (host_header.contains(QLatin1Char(':')) &&
      !host_header.startsWith(QLatin1Char('['))) {
    host_header = QLatin1Char('[') + host_header + QLatin1Char(']');
  }
  QByteArray request;
  request += "GET /" + mountpoint.toUtf8() + " HTTP/1.0\r\n";
  request += "Host: " + host_header.toUtf8() + ":" +
             QByteArray::number(endpoint.port) + "\r\n";
  request += "User-Agent: NTRIP PrismViewer/1.0\r\n";
  request += "Ntrip-Version: Ntrip/2.0\r\n";
  request += "Authorization: Basic " + credentials + "\r\n";
  request += "Ntrip-GGA: " + gga + "\r\n";
  request += "Connection: close\r\n\r\n";
  return request;
}

NtripResponseInspection inspectNtripResponse(const QByteArray& bytes) {
  NtripResponseInspection result;
  const int line_end = bytes.indexOf("\r\n");
  if (line_end < 0) {
    if (bytes.size() > kMaximumNtripHeaderBytes) {
      result.complete = true;
      result.error = QStringLiteral("NTRIP response header is too large");
    }
    return result;
  }
  const QString first_line =
      QString::fromLatin1(bytes.left(line_end)).trimmed();
  const bool icy_ok = first_line.startsWith(
      QStringLiteral("ICY 200"), Qt::CaseInsensitive);
  const bool http_ok =
      first_line.startsWith(QStringLiteral("HTTP/"), Qt::CaseInsensitive) &&
      first_line.contains(QStringLiteral(" 200"));
  if (!icy_ok && !http_ok) {
    result.complete = true;
    result.error =
        first_line.startsWith(QStringLiteral("SOURCETABLE"),
                              Qt::CaseInsensitive)
            ? QStringLiteral("Caster returned a source table instead of RTCM")
            : QStringLiteral("NTRIP caster rejected the request: %1")
                  .arg(first_line);
    return result;
  }
  const int header_end = bytes.indexOf("\r\n\r\n");
  if (header_end >= 0) {
    result.complete = true;
    result.accepted = true;
    result.body_offset = header_end + 4;
    return result;
  }
  const int possible_body = line_end + 2;
  if (bytes.size() > possible_body &&
      static_cast<quint8>(bytes.at(possible_body)) == 0xd3u) {
    result.complete = true;
    result.accepted = true;
    result.body_offset = possible_body;
  } else if (bytes.size() > kMaximumNtripHeaderBytes) {
    result.complete = true;
    result.error = QStringLiteral("NTRIP response header is too large");
  }
  return result;
}

}  // namespace prism_viewer::cors
