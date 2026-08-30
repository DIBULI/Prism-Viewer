#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QDateTime>
#include <QtCore/QString>
#include <QtCore/QVector>

namespace prism_viewer::cors {

struct CorsEndpoint {
  QString name;
  QString host;
  quint16 port = 0;
};

struct CorsMountpoint {
  QString id;
  QString display_name;
};

// IDs are persisted in the serviceProvider setting. Future providers such as
// Qianxun are added as catalog entries without changing the session contract.
struct CorsServiceProvider {
  QString id;
  QString display_name;
  QVector<CorsEndpoint> endpoints;
  QVector<CorsMountpoint> mountpoints;
};

const QVector<CorsServiceProvider>& corsServiceProviders();
const CorsServiceProvider* findCorsServiceProvider(const QString& id);

struct CorsConfiguration {
  QString service_provider;
  QVector<CorsEndpoint> endpoints;
  QString mountpoint;
  QString username;
  QString password;
  double latitude_degrees = 0.0;
  double longitude_degrees = 0.0;
  double altitude_meters = 0.0;
};

QString validateCorsConfiguration(const CorsConfiguration& configuration);
QByteArray buildNmeaGga(const QDateTime& utc, double latitude_degrees,
                        double longitude_degrees, double altitude_meters);
QByteArray buildNtripRequest(const CorsConfiguration& configuration,
                             const CorsEndpoint& endpoint,
                             const QByteArray& gga);

struct NtripResponseInspection {
  bool complete = false;
  bool accepted = false;
  int body_offset = 0;
  QString error;
};

NtripResponseInspection inspectNtripResponse(const QByteArray& bytes);

}  // namespace prism_viewer::cors
