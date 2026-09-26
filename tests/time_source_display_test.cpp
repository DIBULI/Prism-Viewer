#include "ui/device_info_panel.hpp"
#include "ui/time_source_text.hpp"
#include <QtWidgets/QApplication>
#include <QtWidgets/QTableWidget>
#include <cstdlib>
#include <iostream>

using prism_viewer::communication::TimeSyncProvider;
using prism_viewer::common::setChineseUi;
using prism_viewer::common::uiText;
using prism_viewer::ui::timeSourceText;

void require(bool ok, const char* message) {
  if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  for (bool chinese : {false, true}) {
    setChineseUi(chinese);
    prism_viewer::ui::DeviceInfoPanel panel;
    panel.setDeviceOpen(true);
    prism::DeviceInfo info{};
    info.sensor_board_online = true;
    info.sensor_board_time_synced = true;
    panel.setInfo(info,TimeSyncProvider::Gps);
    prism::TimeSyncRtkVersions versions;
    versions.linked=true;versions.application={true,false,1,0,0};versions.bootloader={true,false,0,1,1};
    panel.setRtkModuleVersions(versions);
    auto versionValue=[&] {
      auto* table=panel.findChild<QTableWidget*>();
      for(int row=0;row<table->rowCount();++row)
        if(table->item(row,1)->text()==uiText("RTK-module firmware","RTK-module 固件版本"))return table->item(row,2)->text();
      return QString();
    };
    require(versionValue()==QStringLiteral("1.0.0"),"DeviceInfo RTK-module version missing");
    versions.linked=false;panel.setRtkModuleVersions(versions);
    require(versionValue()==uiText("Unavailable","未提供"),"DeviceInfo retained disconnected module version");
    // Exercise acquire, loss/holdover, host-set UTC and later relock.
    for (auto source : {TimeSyncProvider::SensorBoardInternal,
                        TimeSyncProvider::Gps, TimeSyncProvider::SensorBoardInternal,
                        TimeSyncProvider::RkPtp, TimeSyncProvider::Gps,
                        TimeSyncProvider::LegacyUnknown}) {
      panel.setInfo(info, source);
      auto* table = panel.findChild<QTableWidget*>();
      require(table != nullptr, "DeviceInfo table missing");
      bool found = false;
      const QString expected = source == TimeSyncProvider::Gps
          ? uiText("External time", "外部时间")
          : source == TimeSyncProvider::LegacyUnknown
            ? uiText("Unknown", "未知") : uiText("Internal time", "内部时间");
      for (int row = 0; row < table->rowCount(); ++row) {
        if (table->item(row, 1)->text() == uiText("Time source", "时间来源")) {
          require(table->item(row, 2)->text() == expected, "Wrong public time mode");
          found = true;
        }
        require(!table->item(row, 2)->text().contains(QStringLiteral("Host via")),
                "Raw provider leaked to DeviceInfo");
      }
      require(found, "Time source row missing");
    }
    require(timeSourceText(TimeSyncProvider::Gps, true, false) ==
            uiText("Internal time", "内部时间"), "Lost sync must not show external");
    require(timeSourceText(TimeSyncProvider::Gps, false, true) ==
            uiText("Unknown", "未知"), "Offline must not show external");
    require(timeSourceText(static_cast<TimeSyncProvider>(42), true, true) ==
            uiText("Unknown", "未知"), "Unknown source must stay unknown");
    panel.setDeviceOpen(false);
    require(panel.findChild<QTableWidget*>()->rowCount() == 0,
            "Disconnected panel retains stale source");
  }
  std::cout << "PASS: bilingual internal/external time display; raw provenance hidden; loss/relock/unknown/offline\n";
}
