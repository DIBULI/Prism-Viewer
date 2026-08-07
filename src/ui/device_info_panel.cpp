#include "ui/device_info_panel.hpp"

#include "common/ui_text.hpp"
#include "prism/usb/device_info.hpp"

#include <QtCore/QStringList>
#include <QtGui/QBrush>
#include <QtGui/QColor>
#include <QtWidgets/QAbstractItemView>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QTableWidget>
#include <QtWidgets/QTableWidgetItem>
#include <QtWidgets/QVBoxLayout>

#include <array>
#include <vector>

namespace prism_viewer::ui {
namespace {

using prism_viewer::common::toQString;
using prism_viewer::common::uiText;

struct DeviceInfoRow {
  QString section;
  QString field;
  QString value;
};

QString yesNo(bool value) {
  return value ? uiText("Yes", "是") : uiText("No", "否");
}

QString runningStopped(bool value) {
  return value ? uiText("Running", "运行中")
               : uiText("Stopped", "已停止");
}

QString maskText(uint8_t value) {
  return QStringLiteral("0x%1")
      .arg(static_cast<unsigned int>(value), 2, 16, QLatin1Char('0'));
}

QString deviceListText(uint8_t mask, int device_count,
                       const QString& device_name) {
  QStringList devices;
  for (int index = 0; index < device_count; ++index) {
    if ((mask & static_cast<uint8_t>(1u << index)) != 0u) {
      devices.append(QStringLiteral("%1 %2").arg(device_name).arg(index));
    }
  }
  const QString readable =
      devices.isEmpty() ? uiText("None", "无") : devices.join(QStringLiteral(", "));
  return QStringLiteral("%1 (%2)").arg(readable, maskText(mask));
}

QString cameraStreamingText(uint8_t mask) {
  const uint8_t camera_mask = static_cast<uint8_t>(mask & 0x0fu);
  if (camera_mask == 0u) {
    return uiText("Stopped (%1)", "已停止（%1）").arg(maskText(mask));
  }
  if (camera_mask == 0x0fu) {
    return uiText("All four cameras streaming (%1)",
                  "四路相机全部传输中（%1）")
        .arg(maskText(mask));
  }
  return uiText("Abnormal partial streaming: %1",
                "异常：仅部分相机传输（%1）")
      .arg(deviceListText(camera_mask, 4, uiText("Camera", "相机")));
}

QString valueOrDash(const std::string& value) {
  return value.empty() ? QStringLiteral("-") : toQString(value);
}

QString sensorBoardErrorText(const prism::DeviceInfo& info) {
  if (info.sensor_board_error_flags == 0u) return QStringLiteral("-");
  const QString reason =
      info.sensor_board_error.empty()
          ? QString::fromLatin1(
                prism::sensorBoardErrorCodeName(info.sensor_board_error_code))
          : toQString(info.sensor_board_error);
  return QStringLiteral("%1 (code=%2, flags=0x%3)")
      .arg(reason)
      .arg(static_cast<unsigned int>(info.sensor_board_error_code))
      .arg(info.sensor_board_error_flags, 8, 16, QLatin1Char('0'));
}

}  // namespace

DeviceInfoPanel::DeviceInfoPanel(QWidget* parent) : QWidget(parent) {
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(8, 8, 8, 8);
  root->setSpacing(10);

  auto* group =
      new QGroupBox(uiText("Device Information", "设备信息"), this);
  auto* group_layout = new QVBoxLayout(group);
  group_layout->setSpacing(10);

  auto* header = new QHBoxLayout();
  summary_label_ = new QLabel(group);
  summary_label_->setWordWrap(true);
  summary_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  refresh_button_ =
      new QPushButton(uiText("Refresh DeviceInfo", "刷新设备信息"), group);
  refresh_button_->setMinimumWidth(150);
  version_refresh_button_ =
      new QPushButton(uiText("Refresh Versions", "刷新版本"), group);
  version_refresh_button_->setMinimumWidth(130);
  header->addWidget(summary_label_, 1);
  header->addWidget(version_refresh_button_, 0, Qt::AlignTop);
  header->addWidget(refresh_button_, 0, Qt::AlignTop);
  group_layout->addLayout(header);

  table_ = new QTableWidget(0, 3, group);
  table_->setHorizontalHeaderLabels({
      uiText("Section", "分类"),
      uiText("Field", "字段"),
      uiText("Value", "值"),
  });
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setSelectionMode(QAbstractItemView::SingleSelection);
  table_->setAlternatingRowColors(true);
  table_->verticalHeader()->setVisible(false);
  table_->horizontalHeader()->setSectionResizeMode(
      0, QHeaderView::ResizeToContents);
  table_->horizontalHeader()->setSectionResizeMode(
      1, QHeaderView::ResizeToContents);
  table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
  table_->setTextElideMode(Qt::ElideMiddle);
  group_layout->addWidget(table_, 1);

  root->addWidget(group, 1);

  connect(refresh_button_, &QPushButton::clicked, this, [this]() {
    if (on_refresh) on_refresh();
  });
  connect(version_refresh_button_, &QPushButton::clicked, this, [this]() {
    if (on_refresh_versions) on_refresh_versions();
  });

  clear();
}

void DeviceInfoPanel::clear() {
  info_ = {};
  has_info_ = false;
  has_versions_ = false;
  controls_locked_ = false;
  error_.clear();
  version_error_.clear();
  refreshView();
}

void DeviceInfoPanel::setDeviceOpen(bool open) {
  device_open_ = open;
  if (!open) clear();
  refreshView();
}

void DeviceInfoPanel::setControlsLocked(bool locked) {
  controls_locked_ = locked;
  refreshView();
}

void DeviceInfoPanel::setInfo(const prism::DeviceInfo& info) {
  info_ = info;
  has_info_ = true;
  error_.clear();
  refreshView();
}

void DeviceInfoPanel::setVersions(const prism::DeviceVersions& versions) {
  versions_ = versions;
  has_versions_ = true;
  version_error_.clear();
  refreshView();
}

void DeviceInfoPanel::setError(const QString& error) {
  error_ = error;
  refreshView();
}

void DeviceInfoPanel::setVersionError(const QString& error) {
  has_versions_ = false;
  version_error_ = error;
  refreshView();
}

void DeviceInfoPanel::refreshView() {
  refresh_button_->setEnabled(device_open_ && !controls_locked_);
  version_refresh_button_->setEnabled(device_open_ && !controls_locked_);

  if (!device_open_) {
    summary_label_->setText(
        uiText("Open a Prism USB device to read DeviceInfo.",
               "请先打开 Prism USB 设备以读取设备信息。"));
    summary_label_->setStyleSheet(QStringLiteral(
        "background: #f2f4f7; color: #475467; border: 1px solid #d0d5dd;"
        "border-radius: 6px; padding: 9px 11px; font-weight: 600;"));
    table_->setRowCount(0);
    return;
  }

  if (!error_.isEmpty()) {
    summary_label_->setText(
        uiText("DeviceInfo refresh failed: %1", "设备信息刷新失败：%1")
            .arg(error_));
    summary_label_->setStyleSheet(QStringLiteral(
        "background: #fef3f2; color: #b42318; border: 1px solid #fecdca;"
        "border-radius: 6px; padding: 9px 11px; font-weight: 600;"));
  } else if (!has_info_) {
    summary_label_->setText(
        uiText("Waiting for DeviceInfo...", "正在等待设备信息…"));
    summary_label_->setStyleSheet(QStringLiteral(
        "background: #fffaeb; color: #b54708; border: 1px solid #fedf89;"
        "border-radius: 6px; padding: 9px 11px; font-weight: 600;"));
    table_->setRowCount(0);
    return;
  } else {
    summary_label_->setText(
        uiText("Serial %1 | Agent %2 | sensor-board firmware %3 | USB %4 | "
               "%5 IMU(s) | %6 camera(s)",
               "序列号 %1 | Agent %2 | sensor-board 固件 %3 | USB %4 | "
               "%5 个 IMU | %6 个相机")
            .arg(valueOrDash(info_.product_serial))
            .arg(has_versions_ ? valueOrDash(versions_.agent)
                               : QStringLiteral("-"))
            .arg(has_versions_ ? valueOrDash(versions_.sensor_board)
                               : QStringLiteral("-"))
            .arg(QString::fromLatin1(
                prism::usbLinkSpeedName(info_.usb_speed)))
            .arg(info_.detected_imu_count)
            .arg(info_.detected_camera_count));
    summary_label_->setStyleSheet(
        info_.sensor_board_error_flags != 0u
            ? QStringLiteral(
                  "background: #fef3f2; color: #b42318;"
                  "border: 1px solid #fecdca; border-radius: 6px;"
                  "padding: 9px 11px; font-weight: 600;")
            : info_.sensor_board_online
            ? QStringLiteral(
                  "background: #ecfdf3; color: #027a48;"
                  "border: 1px solid #abefc6; border-radius: 6px;"
                  "padding: 9px 11px; font-weight: 600;")
            : QStringLiteral(
                  "background: #fffaeb; color: #b54708;"
                  "border: 1px solid #fedf89; border-radius: 6px;"
                  "padding: 9px 11px; font-weight: 600;"));
  }

  std::vector<DeviceInfoRow> rows{
      {uiText("Identity", "身份"),
       uiText("Product serial", "产品序列号"),
       valueOrDash(info_.product_serial)},
      {uiText("Versions", "版本"), QStringLiteral("Agent"),
       has_versions_ ? valueOrDash(versions_.agent)
                     : (version_error_.isEmpty()
                            ? uiText("Waiting", "等待读取")
                            : version_error_)},
      {uiText("Versions", "版本"), QStringLiteral("sensor-board"),
       has_versions_ ? valueOrDash(versions_.sensor_board)
                     : (version_error_.isEmpty()
                            ? uiText("Waiting", "等待读取")
                            : version_error_)},
      {uiText("Versions", "版本"), uiText("Combined", "组合版本"),
       has_versions_ ? valueOrDash(versions_.combined)
                     : QStringLiteral("-")},
      {QStringLiteral("USB"), uiText("Link speed", "连接速率"),
       QString::fromLatin1(prism::usbLinkSpeedName(info_.usb_speed))},
      {QStringLiteral("USB"), uiText("USB 3 connected", "USB 3 已连接"),
       yesNo(info_.usb3_connected)},
      {QStringLiteral("sensor-board"), uiText("Online", "在线"),
       yesNo(info_.sensor_board_online)},
      {QStringLiteral("sensor-board"), uiText("Time synchronized", "时间已同步"),
       yesNo(info_.sensor_board_time_synced)},
      {QStringLiteral("sensor-board"), uiText("Transfer error", "传输错误"),
       sensorBoardErrorText(info_)},
      {QStringLiteral("IMU"), uiText("Detected count", "检测数量"),
       QString::number(info_.detected_imu_count)},
      {QStringLiteral("IMU"), uiText("Detected IMUs", "已检测到的 IMU"),
       deviceListText(info_.imu_present_mask, 2, QStringLiteral("IMU"))},
      {QStringLiteral("IMU"), uiText("IMUs receiving data", "正在接收数据的 IMU"),
       deviceListText(info_.imu_receiving_mask, 2, QStringLiteral("IMU"))},
      {QStringLiteral("IMU"),
       uiText("Time-synchronized IMUs", "时间已同步的 IMU"),
       deviceListText(info_.imu_time_synced_mask, 2, QStringLiteral("IMU"))},
      {QStringLiteral("IMU"), uiText("Configured FPS", "配置帧率"),
       QString::number(info_.imu_fps)},
      {uiText("Camera", "相机"), uiText("Detected count", "检测数量"),
       QString::number(info_.detected_camera_count)},
      {uiText("Camera", "相机"),
       uiText("Detected cameras", "已检测到的相机"),
       deviceListText(info_.camera_present_mask, 4,
                      uiText("Camera", "相机"))},
      {uiText("Camera", "相机"),
       uiText("Streaming status", "四路传输状态"),
       cameraStreamingText(info_.camera_streaming_mask)},
      {uiText("Camera", "相机"), uiText("Configured FPS", "配置帧率"),
       QString::number(info_.camera_fps)},
      {QStringLiteral("Wi-Fi"), uiText("Adapter detected", "检测到设备"),
       yesNo(info_.wifi.present)},
      {QStringLiteral("Wi-Fi"), uiText("Enabled", "已启用"),
       yesNo(info_.wifi.enabled)},
      {QStringLiteral("Wi-Fi"), uiText("Access point running", "热点运行中"),
       yesNo(info_.wifi.access_point_running)},
      {QStringLiteral("Wi-Fi"), uiText("Interface", "接口"),
       valueOrDash(info_.wifi.interface_name)},
      {QStringLiteral("Wi-Fi"), QStringLiteral("SSID"),
       valueOrDash(info_.wifi.ssid)},
      {QStringLiteral("Wi-Fi"), uiText("Address", "地址"),
       valueOrDash(info_.wifi.address)},
      {QStringLiteral("Wi-Fi"), QStringLiteral("AP / DHCP"),
       QStringLiteral("%1 / %2")
           .arg(runningStopped(info_.wifi.access_point_running),
                runningStopped(info_.wifi.dhcp_running))},
      {QStringLiteral("Wi-Fi"), uiText("Error", "错误"),
       info_.wifi.error_code == 0 && info_.wifi.error.empty()
           ? QStringLiteral("-")
           : QStringLiteral("%1: %2")
                 .arg(info_.wifi.error_code)
                 .arg(valueOrDash(info_.wifi.error))},
  };

  table_->setRowCount(static_cast<int>(rows.size()));
  for (int row = 0; row < static_cast<int>(rows.size()); ++row) {
    const auto& source = rows[static_cast<size_t>(row)];
    auto* section = new QTableWidgetItem(source.section);
    auto* field = new QTableWidgetItem(source.field);
    auto* value = new QTableWidgetItem(source.value);
    section->setForeground(QBrush(QColor(QStringLiteral("#475467"))));
    section->setBackground(QBrush(QColor(QStringLiteral("#f8fafc"))));
    table_->setItem(row, 0, section);
    table_->setItem(row, 1, field);
    table_->setItem(row, 2, value);
  }
}

}  // namespace prism_viewer::ui
