#include "ui/cors_panel.hpp"

#include <QtCore/QSettings>
#include <QtCore/QTemporaryDir>
#include <QtCore/QStringList>
#include <QtWidgets/QApplication>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QTabWidget>

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
  ok &= require(qobject_cast<QHBoxLayout*>(panel.layout()) != nullptr,
                "CORS page is not arranged as three vertical columns");
  ok &= require(panel.findChild<QGroupBox*>(
                    QStringLiteral("corsConfigurationColumn")) != nullptr &&
                    panel.findChild<QGroupBox*>(
                        QStringLiteral("corsRtkStatusColumn")) != nullptr &&
                    panel.findChild<QGroupBox*>(
                        QStringLiteral("gpsGnssStatusColumn")) != nullptr,
                "One or more CORS/RTK/GPS columns are missing");
  auto* position_tabs =
      panel.findChild<QTabWidget*>(QStringLiteral("gnssRtkTabs"));
  auto* gps_scroll =
      panel.findChild<QScrollArea*>(QStringLiteral("gpsStatusScroll"));
  auto* rtk_scroll =
      panel.findChild<QScrollArea*>(QStringLiteral("rtkStatusScroll"));
  ok &= require(position_tabs != nullptr && position_tabs->count() == 2 &&
                    position_tabs->widget(0)->objectName() ==
                        QStringLiteral("gpsStatusTab") &&
                    position_tabs->widget(1)->objectName() ==
                        QStringLiteral("rtkStatusTab"),
                "GPS and RTK status are not separated into two tabs");
  ok &= require(gps_scroll != nullptr && rtk_scroll != nullptr &&
                    gps_scroll->widgetResizable() &&
                    rtk_scroll->widgetResizable() &&
                    gps_scroll->horizontalScrollBarPolicy() ==
                        Qt::ScrollBarAlwaysOff &&
                    rtk_scroll->horizontalScrollBarPolicy() ==
                        Qt::ScrollBarAlwaysOff,
                "GPS or RTK status tab is missing vertical scrolling");
  if (!ok) return 1;
  panel.resize(1500, 800);
  panel.show();
  application.processEvents();
  const auto verify_tab_fields = [&](int tab, const QStringList& names) {
    position_tabs->setCurrentIndex(tab);
    application.processEvents();
    for (const auto& name : names) {
      auto* value = panel.findChild<QLabel*>(name);
      if (!require(value && position_tabs->widget(tab)->isAncestorOf(value) &&
                       value->isVisible(),
                   "Status field is missing or assigned to the wrong tab")) {
        return false;
      }
      position_tabs->setCurrentIndex(1 - tab);
      application.processEvents();
      if (!require(!value->isVisible(), "Status field leaked into the other tab"))
        return false;
      position_tabs->setCurrentIndex(tab);
    }
    return true;
  };
  ok &= verify_tab_fields(0, {"gnssReceiverQuality", "gnssDop",
                              "gnssPosition", "gnssUtc", "gnssTimingQuality"});
  ok &= verify_tab_fields(1, {"rtkPosition", "rtkRawPosition",
                              "rtkSolutionEpoch", "rtkPrecision", "rtkConfidence"});
  auto* provider =
      panel.findChild<QComboBox*>(QStringLiteral("corsServiceProvider"));
  ok &= require(provider != nullptr, "serviceProvider selector is missing");
  if (provider != nullptr) {
    ok &= require(provider->currentData().toString() ==
                      QStringLiteral("china_mobile"),
                  "China Mobile is not the configured provider");
  }

  auto* gnss_baud =
      panel.findChild<QComboBox*>(QStringLiteral("gnssUartBaudCombo"));
  auto* gnss_refresh = panel.findChild<QPushButton*>(
      QStringLiteral("gnssSettingsRefreshButton"));
  auto* gnss_apply = panel.findChild<QPushButton*>(
      QStringLiteral("gnssSettingsApplyButton"));
  ok &= require(gnss_baud != nullptr && gnss_refresh != nullptr &&
                    gnss_apply != nullptr,
                "GNSS input controls are missing from the CORS/RTK page");
  if (gnss_baud != nullptr && gnss_refresh != nullptr &&
      gnss_apply != nullptr) {
    ok &= require(!gnss_baud->isEnabled() && !gnss_apply->isEnabled(),
                  "GNSS input controls are enabled before device open");
    prism::DeviceConfiguration loaded;
    loaded.camera_fps = 20;
    loaded.mjpeg_quality = 92;
    loaded.gnss_uart_baud = 921600;
    loaded.generation = 7;
    loaded.persisted = true;
    panel.setDeviceOpen(true);
    panel.setDeviceConfiguration(loaded);
    ok &= require(gnss_baud->currentData().toUInt() == 921600u,
                  "Loaded GNSS UART baud is not selected");
    ok &= require(!gnss_apply->isEnabled(),
                  "Unchanged GNSS UART baud can be saved");
    prism::DeviceConfiguration requested;
    bool apply_called = false;
    panel.on_gnss_apply =
        [&](const prism::DeviceConfiguration& configuration) {
          apply_called = true;
          requested = configuration;
        };
    gnss_baud->setCurrentIndex(gnss_baud->findData(460800u));
    ok &= require(gnss_apply->isEnabled(),
                  "Changed GNSS UART baud cannot be saved");
    gnss_apply->click();
    ok &= require(apply_called && requested.gnss_uart_baud == 460800u &&
                      requested.camera_fps == 20u,
                  "GNSS apply callback did not preserve device settings");
    panel.setControlsLocked(true);
    ok &= require(!gnss_baud->isEnabled() && !gnss_apply->isEnabled(),
                  "GNSS controls remain enabled while operations are locked");
    panel.setControlsLocked(false);
  }

  auto setText = [&](const char* object_name, const QString& value) {
    auto* edit =
        panel.findChild<QLineEdit*>(QString::fromLatin1(object_name));
    ok &= require(edit != nullptr, object_name);
    if (edit != nullptr) edit->setText(value);
  };
  setText("corsUsername", QStringLiteral("test-user"));
  setText("corsPassword", QStringLiteral("test-password"));
  ok &= require(panel.findChild<QLineEdit*>(QStringLiteral("corsLatitude")) ==
                        nullptr &&
                    panel.findChild<QLineEdit*>(
                        QStringLiteral("corsLongitude")) == nullptr &&
                    panel.findChild<QLineEdit*>(QStringLiteral("corsAltitude")) ==
                        nullptr,
                "Manual rover position controls are still present");
  prism::GnssTimingStatus configuration_timing;
  configuration_timing.nmea_seen = true;
  configuration_timing.nmea_fix_valid = true;
  configuration_timing.nmea_position_valid = true;
  configuration_timing.nmea_fix_quality = 1;
  configuration_timing.satellites = 12;
  configuration_timing.nmea_age_ms = 25;
  configuration_timing.latitude_e7 = 312304000;
  configuration_timing.longitude_e7 = 1214737000;
  configuration_timing.altitude_mm = 15200;
  configuration_timing.utc_ms_of_day = 45319125;
  panel.setGnssTimingStatus(configuration_timing);

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
  ok &= require(configuration.latitude_degrees == 31.2304 &&
                    configuration.longitude_degrees == 121.4737 &&
                    configuration.altitude_meters == 15.2,
                "Panel did not source the CORS position from device GNSS");

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
  navigation.smoothed_position_valid = true;
  navigation.smoothed_solution =
      prism_viewer::communication::RtkSolution::Fix;
  navigation.smoothing_flags =
      prism_viewer::communication::RtkSmoothingDynamicsEnabled;
  navigation.smoothed_solution_epoch_us = navigation.solution_epoch_us;
  navigation.smoothed_latitude_deg = 31.230400001;
  navigation.smoothed_longitude_deg = 121.473700001;
  navigation.smoothed_ellipsoidal_height_m = 12.49;
  navigation.smoothed_east_std_m = 0.008;
  navigation.smoothed_north_std_m = 0.012;
  navigation.smoothed_up_std_m = 0.019;
  navigation.smoothing_reset_count = 3u;
  navigation.smoothing_gated_epoch_count = 2u;
  panel.setNavigationStatus(navigation);
  prism::GnssTimingStatus timing;
  timing.sensor_board_online = true;
  timing.gnss_input_mode = true;
  timing.time_synced = true;
  timing.offset_fresh = true;
  timing.message_pps_offset_us = 103700;
  timing.last_pps_epoch_us = 1780000000000000ULL;
  timing.pps_detected = true;
  timing.pps_valid = true;
  timing.pps_high_width_us = 100000;
  timing.nmea_seen = true;
  timing.nmea_fix_valid = true;
  timing.nmea_dop_valid = true;
  timing.nmea_fix_quality = 1;
  timing.nmea_fix_mode = 3;
  timing.satellites = 12;
  timing.pdop_milli = 4800;
  timing.hdop_milli = 2800;
  timing.vdop_milli = 3900;
  timing.nmea_age_ms = 25;
  timing.nmea_update_count = 91;
  timing.nmea_position_valid = true;
  timing.latitude_e7 = 312304000;
  timing.longitude_e7 = 1214737000;
  timing.altitude_mm = 15200;
  timing.geoid_separation_mm = -7300;
  timing.utc_ms_of_day = 45319125;
  panel.setGnssTimingStatus(timing);
  const auto live_configuration = panel.configuration(&error);
  ok &= require(error.isEmpty() &&
                    live_configuration.latitude_degrees == 31.2304 &&
                    live_configuration.longitude_degrees == 121.4737 &&
                    live_configuration.altitude_meters == 15.2,
                "Panel did not prefer fresh device GNSS for CORS GGA");
  navigation.solution_epoch_us += 100000LL;
  navigation.smoothed_solution_epoch_us += 100000LL;
  ++navigation.solution_count;
  panel.setNavigationStatus(navigation);
  panel.setDeviceTimeUs(
      static_cast<uint64_t>(navigation.solution_epoch_us + 1250000LL));
  auto labelText = [&](const char* object_name) {
    auto* label =
        panel.findChild<QLabel*>(QString::fromLatin1(object_name));
    ok &= require(label != nullptr, object_name);
    return label == nullptr ? QString() : label->text();
  };
  ok &= require(labelText("rtkUpdateRate").contains(QStringLiteral("10.00 Hz")),
                "Panel did not report the solution-epoch navigation rate");
  ok &= require(labelText("rtkSolutionEpoch").contains(
                    QStringLiteral("s ago (device clock)")),
                "Panel did not report solution age against device time");
  ok &= require(labelText("rtkPosition").contains(QStringLiteral("31.230400001")) &&
                    labelText("rtkPosition").contains(
                        QStringLiteral("Satellites: 18")) &&
                    labelText("rtkPosition").contains(
                        QStringLiteral("Local ENU (m): E=")),
                "Panel did not display smoothed RTK position and satellites");
  ok &= require(labelText("rtkRawPosition").contains(
                    QStringLiteral("31.230400000")) &&
                    labelText("rtkRawPosition").contains(
                        QStringLiteral("Local ENU (m): E=")),
                "Panel did not preserve the raw RTK position");
  ok &= require(labelText("rtkConfidence").contains(QStringLiteral("950/1000")),
                "Panel did not display RTK credibility");
  navigation.rover_observation_epochs = 205u;
  navigation.base_observation_epochs = 101u;
  panel.setNavigationStatus(navigation);
  ok &= require(
      labelText("rtkAgentCounters").contains(
          QStringLiteral("rover epochs=205 base epochs=101")) &&
          labelText("rtkAgentCounters").contains(
              QStringLiteral("smoothing=active resets=3 gated=2")),
      "Panel did not refresh Agent counters without a new solution");
  ok &= require(labelText("gnssReceiverQuality").contains(
                    QStringLiteral("12 satellites")) &&
                    labelText("gnssReceiverQuality").contains(
                        QStringLiteral("3D")),
                "Panel did not display standalone GNSS quality");
  ok &= require(labelText("gnssDop").contains(QStringLiteral("PDOP=4.80")) &&
                    labelText("gnssDop").contains(QStringLiteral("HDOP=2.80")),
                "Panel did not display GNSS DOP values");
  ok &= require(labelText("gnssPosition").contains(
                    QStringLiteral("31.2304000")) &&
                    labelText("gnssPosition").contains(
                        QStringLiteral("121.4737000")) &&
                    labelText("gnssPosition").contains(
                        QStringLiteral("15.200 m")) &&
                    labelText("gnssPosition").contains(
                        QStringLiteral("Local ENU (m): E=")) &&
                    labelText("localEnuOrigin").contains(
                        QStringLiteral("first valid GPS fix")),
                "Panel did not display detailed GGA position");
  ok &= require(labelText("gnssUtc").contains(
                    QStringLiteral("12:35:19.125 UTC")),
                "Panel did not display detailed GGA UTC time");
  ok &= require(labelText("gnssTimingQuality").contains(
                    QStringLiteral("PPS high 100.000 ms (valid)")) &&
                    labelText("gnssTimingQuality").contains(
                        QStringLiteral("NMEA-PPS delay 103.700 ms")) &&
                    !labelText("gnssTimingQuality").contains(
                        QStringLiteral("RK-PPS")),
                "Panel did not limit PPS display to public basic status");
  timing.nmea_fix_valid = false;
  timing.nmea_position_valid = false;
  timing.nmea_fix_quality = 0;
  timing.nmea_fix_mode = 1;
  timing.satellites = 0;
  timing.nmea_age_ms = 25;
  ++timing.nmea_update_count;
  panel.setGnssTimingStatus(timing);
  ok &= require(labelText("gnssReceiverQuality").contains(
                    QStringLiteral("invalid | no fix | 0 satellites")),
                "Panel retained a stale valid fix after GNSS loss");
  return ok ? 0 : 1;
}
