#include "ui/cors_panel.hpp"

#include <QtCore/QSettings>
#include <QtCore/QTemporaryDir>
#include <QtWidgets/QApplication>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QLabel>

#include <iostream>

namespace {

bool require(bool condition, const char* message) {
  if (!condition) std::cerr << message << '\n';
  return condition;
}

}  // namespace

int main(int argc, char** argv) {
  QApplication application(argc, argv);
  QTemporaryDir settings_directory;
  if (!settings_directory.isValid()) return 1;
  QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope,
                     settings_directory.path());

  prism_viewer::ui::CorsPanel panel;
  bool ok = true;
  auto* provider =
      panel.findChild<QComboBox*>(QStringLiteral("corsServiceProvider"));
  ok &= require(provider != nullptr, "serviceProvider selector is missing");
  if (provider != nullptr) {
    ok &= require(provider->currentData().toString() ==
                      QStringLiteral("china_mobile"),
                  "China Mobile is not the configured provider");
  }

  auto setText = [&](const char* object_name, const QString& value) {
    auto* edit =
        panel.findChild<QLineEdit*>(QString::fromLatin1(object_name));
    ok &= require(edit != nullptr, object_name);
    if (edit != nullptr) edit->setText(value);
  };
  setText("corsUsername", QStringLiteral("test-user"));
  setText("corsPassword", QStringLiteral("test-password"));
  setText("corsLatitude", QStringLiteral("31.2304"));
  setText("corsLongitude", QStringLiteral("121.4737"));
  setText("corsAltitude", QStringLiteral("12.5"));

  QString error;
  const auto configuration = panel.configuration(&error);
  ok &= require(error.isEmpty(), "Panel rejected valid CORS settings");
  ok &= require(configuration.service_provider ==
                    QStringLiteral("china_mobile"),
                "Panel did not emit the stable serviceProvider ID");
  ok &= require(configuration.endpoints.size() == 2,
                "Automatic endpoint policy did not include failover");
  ok &= require(configuration.endpoints.front().port == 8002,
                "WGS84 port was not selected");
  ok &= require(configuration.mountpoint ==
                    QStringLiteral("RTCM33_GRCEJ"),
                "Default China Mobile mountpoint is incorrect");

  auto* endpoint =
      panel.findChild<QComboBox*>(QStringLiteral("corsEndpointPolicy"));
  ok &= require(endpoint != nullptr, "Editable caster endpoint is missing");
  if (endpoint != nullptr) {
    endpoint->setCurrentIndex(-1);
    endpoint->setEditText(
        QStringLiteral("https://cors.example.com:443/CUSTOM_RTCM"));
    const auto manual = panel.configuration(&error);
    ok &= require(error.isEmpty(), "Panel rejected a manual caster URL");
    ok &= require(manual.endpoints.size() == 1 &&
                      manual.endpoints.front().host ==
                          QStringLiteral("cors.example.com") &&
                      manual.endpoints.front().port == 443 &&
                      manual.endpoints.front().tls,
                  "Panel did not emit the manual TLS endpoint");
    ok &= require(manual.mountpoint == QStringLiteral("CUSTOM_RTCM"),
                  "Panel did not use the URL mountpoint");
  }


  prism_viewer::communication::RtkNavigationStatus navigation;
  navigation.solution_valid = true;
  navigation.confidence_valid = true;
  navigation.position_jump_valid = true;
  navigation.base_source =
      prism_viewer::communication::RtkBaseSource::HostCors;
  navigation.solution = prism_viewer::communication::RtkSolution::Fix;
  navigation.confidence =
      prism_viewer::communication::RtkConfidence::High;
  navigation.solution_epoch_us = 1780000000000000LL;
  navigation.solution_count = 100u;
  navigation.latitude_deg = 31.2304;
  navigation.longitude_deg = 121.4737;
  navigation.ellipsoidal_height_m = 12.5;
  navigation.east_std_m = 0.01;
  navigation.north_std_m = 0.02;
  navigation.up_std_m = 0.03;
  navigation.satellites = 18u;
  navigation.confidence_score = 950u;
  navigation.differential_age_s = 0.3;
  navigation.ambiguity_ratio = 4.2;
  navigation.position_jump_m = 0.004;
  panel.setNavigationStatus(navigation);
  navigation.solution_epoch_us += 100000LL;
  ++navigation.solution_count;
  panel.setNavigationStatus(navigation);
  auto labelText = [&](const char* object_name) {
    auto* label =
        panel.findChild<QLabel*>(QString::fromLatin1(object_name));
    ok &= require(label != nullptr, object_name);
    return label == nullptr ? QString() : label->text();
  };
  ok &= require(labelText("rtkUpdateRate").contains(QStringLiteral("10.00 Hz")),
                "Panel did not report the solution-epoch navigation rate");
  ok &= require(labelText("rtkPosition").contains(QStringLiteral("31.230400000")) &&
                    labelText("rtkPosition").contains(QStringLiteral("satellites=18")),
                "Panel did not display GPS/RTK position and satellites");
  ok &= require(labelText("rtkConfidence").contains(QStringLiteral("950/1000")),
                "Panel did not display RTK credibility");
  return ok ? 0 : 1;
}
