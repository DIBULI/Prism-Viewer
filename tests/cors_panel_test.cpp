#include "ui/cors_panel.hpp"
#include "common/ui_text.hpp"
#include "ui/rtk_error_help.hpp"

#include <QtCore/QSettings>
#include <QtCore/QTemporaryDir>
#include <QtCore/QStringList>
#include <QtGui/QPixmap>
#include <QtWidgets/QApplication>
#include <QtWidgets/QDialog>
#include <QtWidgets/QTextBrowser>
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
  // NativeFormat ignores custom paths on macOS; never touch real credentials.
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                     settings_directory.path());

  prism_viewer::ui::CorsPanel panel;
  bool ok = true;
  auto* ts_mode=panel.findChild<QComboBox*>("timesyncMode");
  auto* ts_apply=panel.findChild<QPushButton*>("timesyncApply");
  auto* ts_text=panel.findChild<QLabel*>("timesyncStatus");
  auto* module_versions=panel.findChild<QLabel*>("rtkModuleVersions");
  ok &= require(module_versions!=nullptr,"module version field missing");
  if(!ok)return 1;
  prism::TimeSyncRtkVersions v;
  v.linked=true;v.application={true,false,1,0,0};v.bootloader={true,false,0,1,1};
  for(const bool chinese:{false,true}) {
    prism_viewer::common::setChineseUi(chinese);
    panel.setRtkModuleVersions(v);
    ok &= require(module_versions->text().contains("1.0.0")&&module_versions->text().contains("0.1.1"),"module versions not rendered");
    v.linked=false;panel.setRtkModuleVersions(v);
    ok &= require(!module_versions->text().contains("1.0.0"),"disconnected version remained current");
    v.linked=true;
    panel.setRtkModuleVersions(std::nullopt);
    ok &= require(module_versions->text().contains(chinese?QStringLiteral("未提供"):QStringLiteral("Unavailable")),"missing version translation");
  }
  prism_viewer::common::setChineseUi(false);
  ok &= require(ts_mode && ts_mode->count()==3 && ts_apply && !ts_apply->isEnabled(), "Timesync controls missing or unsafe initial state");
  if (!ok) return 1;
  panel.setDeviceOpen(true);
  prism::TimeSyncPortStatus ts;
  ts.mode=prism::TimeSyncPortMode::Rtk;ts.persisted=true;
  panel.setTimeSyncLocked(false);
  panel.setTimeSyncStatus(ts,std::nullopt,QStringLiteral("unavailable"));
  ok &= require(ts_mode->currentData().toInt()==2 && ts_apply->isEnabled(), "RTK mode not loaded");
  prism::TimeSyncRtkStatus module;
  module.linked = module.device_status_fresh = module.control_status_fresh = true;
  module.error_code = -11;
  module.device_flags = 3;
  module.gnss_age_ms = 50;
  for (const bool chinese : {false, true}) {
    prism_viewer::common::setChineseUi(chinese);
    const QString no_fix = chinese ? QStringLiteral("GNSS 未定位成功") :
        QStringLiteral("GNSS has no position fix");
    panel.setTimeSyncStatus(ts, module, {});
    ok &= require(ts_text->text().contains(no_fix), "Fresh invalid GNSS fix needs an explicit message");
    const auto check_not_no_fix = [&](prism::TimeSyncRtkStatus sample) {
      panel.setTimeSyncStatus(ts, sample, {});
      ok &= require(!ts_text->text().contains(no_fix), "Stale, missing or unrelated state must not imply GNSS fix failure");
    };
    auto sample = module; sample.fix = 1; check_not_no_fix(sample);
    sample = module; sample.gnss_age_ms = 3000; check_not_no_fix(sample);
    sample = module; sample.device_status_fresh = false; check_not_no_fix(sample);
    sample = module; sample.control_status_fresh = false; check_not_no_fix(sample);
    ok &= require(ts_text->text().contains(chinese ? QStringLiteral("已过期 / 未提供") : QStringLiteral("Stale / unavailable")), "Stale control message was hidden");
    sample = module; sample.linked = false; check_not_no_fix(sample);
    sample = module; sample.device_flags = 1; check_not_no_fix(sample);
    sample = module; sample.error_code = -121; check_not_no_fix(sample);
    sample = module; sample.control_error = 2; sample.control_state = 5; check_not_no_fix(sample);
    ok &= require(ts_text->text().contains(chinese ? QStringLiteral("控制状态：错误") : QStringLiteral("Control: error")), "Real control error was hidden");
    panel.setTimeSyncStatus(ts, std::nullopt, QStringLiteral("unavailable"));
    ok &= require(!ts_text->text().contains(no_fix), "Absent module must not imply GNSS fix failure");
  }
  prism_viewer::common::setChineseUi(false);
  auto* error_help = panel.findChild<QPushButton*>(QStringLiteral("rtkErrorHelp"));
  ok &= require(error_help != nullptr, "RTK error help button missing");
  if (!ok) return 1;
  unsigned commands = 0;
  panel.on_timesync = [&](auto) { ++commands; };
  for (const bool chinese : {false, true}) {
    prism_viewer::common::setChineseUi(chinese);
    for (const auto& entry : prism_viewer::ui::rtkErrorEntries)
      ok &= require(prism_viewer::ui::rtkErrorExplanation(entry.kind, entry.code) == QString::fromUtf8(chinese ? entry.zh : entry.en), "Incorrect error catalog translation");
    ok &= require(prism_viewer::ui::rtkErrorExplanation("port", -11) != prism_viewer::ui::rtkErrorExplanation("module", -11), "Error domains conflated");
    ts.error_code = -11;
    module.rtcm_errors = 4294967295u;
    panel.setTimeSyncStatus(ts, module, {});
    error_help->click();application.processEvents();
    auto* dialog = panel.findChild<QDialog*>(QStringLiteral("rtkErrorDialog"));
    ok &= require(dialog && dialog->isVisible(), "RTK error popup did not open");
    if (!dialog) return 1;
    const auto pages = dialog->findChildren<QTextBrowser*>();
    ok &= require(pages.size() == 3, "LED, current status and error reference must be separate pages");
    auto* help_tabs = dialog->findChild<QTabWidget*>();
    ok &= require(help_tabs && help_tabs->tabText(0).contains(QStringLiteral("LED")), "LED reference must be easy to find");
    QString text;for (auto* page : pages) text += page->toPlainText();
    const auto led = prism_viewer::ui::rtkLedReference();
    ok &= require(!led.contains(QStringLiteral("UM980")), "LED help must use generic GNSS/RTK chip terminology");
    ok &= require(QString::fromUtf8(chinese ? prism_viewer::ui::rtkLedEntries[0].zh : prism_viewer::ui::rtkLedEntries[0].en) ==
                      (chinese ? QStringLiteral("GNSS/RTK 芯片未检测到") : QStringLiteral("GNSS/RTK chip not detected")), "Timeout and absent receiver must share one status name");
    ok &= require(text.contains(chinese ? QStringLiteral("超过 3 秒未收到有效数据") : QStringLiteral("more than 3 seconds")), "Missing receiver timeout rule");
    const unsigned periods[] = {2000, 1000, 500, 4000, 125, 2000, 250};
    const unsigned on_times[] = {1000, 500, 250, 200, 62, 100, 125};
    unsigned led_index = 0;
    for (const auto& entry : prism_viewer::ui::rtkLedEntries) {
      ok &= require(entry.period_ms == periods[led_index] && entry.on_ms == on_times[led_index], "LED priority/duration differs from firmware");
      ok &= require(led.contains(QString::fromUtf8(chinese ? entry.zh : entry.en)), "Missing LED translation");
      ++led_index;
    }
    ok &= require(led_index == 7 && text.contains(chinese ? QStringLiteral("状态优先级") : QStringLiteral("Priority is top to bottom")), "Missing LED priority warning");
    ok &= require(text.contains(chinese ? QStringLiteral("并非实时") : QStringLiteral("not a live")), "LED guide must not claim live telemetry");
    ok &= require(text.contains(chinese ? QStringLiteral("硬件及 SIM/入网检查优先于 CORS") : QStringLiteral("checks take priority over CORS")), "Hardware-first LED priority is not explained");
    ok &= require(text.contains(chinese ? QStringLiteral("旧固件") : QStringLiteral("older firmware")), "LED guide must identify the firmware dependency");
    ok &= require(text.contains(QStringLiteral("4294967295")) && text.contains(chinese ? QStringLiteral("累计") : QStringLiteral("Cumulative")), "RTCM counter not explained separately");
    if (argc == 2) dialog->grab().save(QString::fromLocal8Bit(argv[1]) + (chinese ? "-errors-zh.png" : "-errors-en.png"));
    dialog->close();application.sendPostedEvents(nullptr, QEvent::DeferredDelete);
    panel.setTimeSyncError(QStringLiteral("timeout"));error_help->click();application.processEvents();
    dialog = panel.findChild<QDialog*>(QStringLiteral("rtkErrorDialog"));
    text.clear();for (auto* page : dialog->findChildren<QTextBrowser*>()) text += page->toPlainText();
    ok &= require(text.contains(chinese ? QStringLiteral("尚未提供") : QStringLiteral("Not provided")), "Unavailable status treated as success");
    dialog->close();application.sendPostedEvents(nullptr, QEvent::DeferredDelete);
  }
  ok &= require(commands == 0, "Opening error help sent a device command");
  panel.on_timesync = {};
  prism_viewer::common::setChineseUi(false);
  panel.setTimeSyncLocked(true);
  ok &= require(!ts_apply->isEnabled(), "Timesync apply not locked");
  panel.setTimeSyncError(QStringLiteral("timeout"));
  panel.setTimeSyncLocked(false);
  ok &= require(!ts_apply->isEnabled() && ts_text->text()==QStringLiteral("timeout"), "Timesync stale state not cleared");
  panel.setDeviceOpen(false);
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
  ok &= require(position_tabs != nullptr && position_tabs->count() == 1 &&
                    position_tabs->widget(0)->objectName() ==
                        QStringLiteral("gpsStatusTab"),
                "Retired software RTK tab must not exist");
  ok &= require(gps_scroll != nullptr && rtk_scroll == nullptr &&
                    gps_scroll->widgetResizable() &&
                    gps_scroll->horizontalScrollBarPolicy() ==
                        Qt::ScrollBarAlwaysOff,
                "GNSS status must retain vertical scrolling");
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
    }
    return true;
  };
  ok &= verify_tab_fields(0, {"gnssReceiverQuality", "gnssDop",
                              "gnssPosition", "gnssUtc", "gnssTimingQuality",
                              "gnssReceptionStatus", "gnssTimeSyncStatus"});
  if (argc == 2) {
    auto* column = panel.findChild<QGroupBox*>(
        QStringLiteral("gpsGnssStatusColumn"));
    position_tabs->setCurrentIndex(0);
    application.processEvents();
    ok &= column->grab().save(QString::fromLocal8Bit(argv[1]) + "-gps.png");
  }
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

  auto* coordinates =
      panel.findChild<QComboBox*>(QStringLiteral("corsCoordinateSystem"));
  auto* mounts = panel.findChild<QComboBox*>(QStringLiteral("corsMountpoint"));
  auto* caster =
      panel.findChild<QComboBox*>(QStringLiteral("corsEndpointPolicy"));
  if (!provider || !coordinates || !mounts || !caster) return 1;
  coordinates->setCurrentIndex(coordinates->findData(8001));
  provider->setCurrentIndex(provider->findData(QStringLiteral("qianxun")));
  ok &= require(coordinates->currentData().toUInt() == 8003 &&
                    coordinates->findData(8001) == -1 &&
                    caster->count() == 1 && mounts->count() == 3 &&
                    mounts->currentData().toString() == QStringLiteral("AUTO"),
                "Switching to Qianxun retained China Mobile options");
  for (const int port : {8002, 8003}) {
    coordinates->setCurrentIndex(coordinates->findData(port));
    for (const QString& mount : {QStringLiteral("AUTO"),
                                 QStringLiteral("RTCM32_GGB"),
                                 QStringLiteral("RTCM30_GG")}) {
      mounts->setCurrentIndex(mounts->findData(mount));
      const auto requested = panel.configuration(&error);
      ok &= require(error.isEmpty() &&
                        requested.service_provider == QStringLiteral("qianxun") &&
                        requested.endpoints.size() == 1 &&
                        requested.endpoints.front().host == QStringLiteral("203.107.45.154") &&
                        requested.endpoints.front().port == port &&
                        requested.mountpoint == mount,
                    "Qianxun selection did not reach the connection configuration");
    }
  }
  // Save through the real button callback, with no network/session attached.
  QPushButton* connect_cors = nullptr;
  for (auto* button : panel.findChildren<QPushButton*>()) {
    if (button->text() == QStringLiteral("Connect CORS")) connect_cors = button;
  }
  if (!connect_cors) return 1;
  bool connected = false;
  panel.on_connect = [&](const auto& requested) {
    connected = requested.service_provider == QStringLiteral("qianxun") &&
                requested.endpoints.front().port == 8003 &&
                requested.mountpoint == QStringLiteral("RTCM30_GG");
  };
  connect_cors->click();
  ok &= require(connected, "Qianxun connect callback failed");
  {
    prism_viewer::ui::CorsPanel restored;
    restored.setGnssTimingStatus(configuration_timing);
    restored.findChild<QLineEdit*>(QStringLiteral("corsPassword"))->setText(
        QStringLiteral("test-password"));
    const auto saved = restored.configuration(&error);
    ok &= require(error.isEmpty() &&
                      saved.service_provider == QStringLiteral("qianxun") &&
                      saved.endpoints.size() == 1 &&
                      saved.endpoints.front().port == 8003 &&
                      saved.mountpoint == QStringLiteral("RTCM30_GG"),
                  "Qianxun settings were not restored");
  }
  panel.on_connect = {};
  mounts->setCurrentIndex(mounts->findData(QStringLiteral("AUTO")));
  if (argc == 2) {
    application.processEvents();
    auto* column = panel.findChild<QGroupBox*>(QStringLiteral("corsConfigurationColumn"));
    ok &= column->grab().save(QString::fromLocal8Bit(argv[1]) + "-qianxun.png");
  }
  provider->setCurrentIndex(provider->findData(QStringLiteral("china_mobile")));
  ok &= require(coordinates->currentData().toUInt() == 8001 &&
                    caster->count() == 3 && mounts->count() == 5 &&
                    mounts->currentData().toString() == QStringLiteral("RTCM33_GRCEJ"),
                "Switching back did not restore China Mobile CGCS2000/failover");
  coordinates->setCurrentIndex(coordinates->findData(8002));

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
  auto labelText = [&](const char* object_name) {
    auto* label =
        panel.findChild<QLabel*>(QString::fromLatin1(object_name));
    ok &= require(label != nullptr, object_name);
    return label == nullptr ? QString() : label->text();
  };
  ok &= require(panel.findChild<QLabel*>(QStringLiteral("rtkRawPosition")) == nullptr &&
                    panel.findChild<QLabel*>(QStringLiteral("rtkPosition")) == nullptr,
                "Retired raw/smoothed controls must not exist");
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
  timing = {};
  prism::GnssReceptionStatus reception;
  reception.reception_available = true;
  panel.setGnssReceptionStatus(reception);
  timing.sensor_board_online = true;
  timing.pps_detected = true;
  timing.pps_valid = true;
  timing.pps_high_width_us = 100000;
  panel.setGnssTimingStatus(timing);
  ok &= require(labelText("gnssReceptionStatus").contains("no bytes received") &&
                    labelText("gnssTimeSyncStatus").contains("NOT LOCKED") &&
                    labelText("gnssTimingQuality").contains("(valid)"),
                "PPS-only state was not distinguished from reception/lock");
  reception.raw_data_seen = true;
  reception.raw_data_fresh = true;
  panel.setGnssReceptionStatus(reception);
  panel.setGnssTimingStatus(timing);
  ok &= require(labelText("gnssReceptionStatus").contains("receiving bytes") &&
                    labelText("gnssReceptionStatus").contains("no checksum-valid"),
                "raw UART bytes were mistaken for valid NMEA");
  reception.nmea_sentence_seen = true;
  reception.nmea_sentence_fresh = true;
  panel.setGnssReceptionStatus(reception);
  panel.setGnssTimingStatus(timing);
  ok &= require(labelText("gnssReceptionStatus").contains("valid sentences updating") &&
                    labelText("gnssTimeSyncStatus").contains("NOT LOCKED") &&
                    labelText("gnssReceiverQuality").contains("No GGA/GSA"),
                "valid NMEA was incorrectly treated as a fix or UTC lock");
  timing.time_synced = true;
  reception.raw_data_fresh = false;
  reception.nmea_sentence_fresh = false;
  panel.setGnssReceptionStatus(reception);
  panel.setGnssTimingStatus(timing);
  ok &= require(labelText("gnssReceptionStatus").contains("over 2 s") &&
                    labelText("gnssTimeSyncStatus").startsWith("LOCKED:"),
                "stopped UART stream was hidden by retained PPS time lock");
  panel.setGnssReceptionStatus(std::nullopt);
  panel.setGnssTimingStatus(timing);
  ok &= require(labelText("gnssReceptionStatus").contains("unavailable"),
                "old recording was incorrectly labelled no UART reception");
  return ok ? 0 : 1;
}
