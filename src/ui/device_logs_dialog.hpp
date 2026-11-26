#pragma once
#include "prism/usb/device_logs.hpp"
#include <QString>
#include <functional>
class QWidget;
namespace prism_viewer::ui {
using CollectDeviceLogs=std::function<prism::DeviceLogArchive(const prism::DeviceLogProgress&,const prism::DeviceLogCancel&)>;
void showDeviceLogsDialog(QWidget* parent,const QString& destination,CollectDeviceLogs collect);
}
