#include "ui/cors_panel.hpp"

#include <QtCore/QSettings>
#include <QtCore/QTemporaryDir>
#include <QtWidgets/QApplication>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QLineEdit>

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
  return ok ? 0 : 1;
}
