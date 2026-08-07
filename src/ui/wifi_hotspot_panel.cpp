#include "ui/wifi_hotspot_panel.hpp"

#include "common/ui_text.hpp"

#include <QtWidgets/QFormLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QVBoxLayout>

namespace prism_viewer::ui {
namespace {

using prism_viewer::common::uiText;

QString valueOrDash(const QString& value) {
  return value.isEmpty() ? QStringLiteral("-") : value;
}

}  // namespace

WifiHotspotPanel::WifiHotspotPanel(QWidget* parent) : QWidget(parent) {
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(8, 8, 8, 8);
  root->setSpacing(10);

  auto* hotspot_group =
      new QGroupBox(uiText("Wi-Fi Hotspot", "Wi-Fi 热点"), this);
  auto* group_layout = new QVBoxLayout(hotspot_group);
  group_layout->setSpacing(10);

  message_label_ = new QLabel(
      uiText("Open a device to manage its Wi-Fi hotspot",
             "打开设备后可管理 Wi-Fi 热点"),
      hotspot_group);
  message_label_->setWordWrap(true);
  group_layout->addWidget(message_label_);

  auto* details = new QFormLayout();
  details->setHorizontalSpacing(18);
  details->setVerticalSpacing(10);
  detection_value_ = new QLabel(QStringLiteral("-"), hotspot_group);
  interface_value_ = new QLabel(QStringLiteral("-"), hotspot_group);
  state_value_ = new QLabel(QStringLiteral("-"), hotspot_group);
  ssid_value_ = new QLabel(QStringLiteral("-"), hotspot_group);
  security_value_ = new QLabel(
      uiText("Open network (no password)", "开放网络（无密码）"),
      hotspot_group);
  address_value_ = new QLabel(QStringLiteral("10.42.200.1/24"), hotspot_group);
  dhcp_value_ = new QLabel(QStringLiteral("-"), hotspot_group);
  persisted_value_ = new QLabel(QStringLiteral("-"), hotspot_group);

  ssid_value_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  interface_value_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  address_value_->setTextInteractionFlags(Qt::TextSelectableByMouse);

  details->addRow(uiText("Wi-Fi adapter:", "Wi-Fi 设备："),
                  detection_value_);
  details->addRow(uiText("Interface:", "接口："), interface_value_);
  details->addRow(uiText("Hotspot state:", "热点状态："), state_value_);
  details->addRow(QStringLiteral("SSID:"), ssid_value_);
  details->addRow(uiText("Security:", "安全性："), security_value_);

  details->addRow(uiText("Local address:", "本机地址："), address_value_);
  details->addRow(QStringLiteral("DHCP:"), dhcp_value_);
  details->addRow(uiText("Start at boot:", "开机启用："),
                  persisted_value_);
  group_layout->addLayout(details);

  auto* actions = new QHBoxLayout();
  refresh_button_ =
      new QPushButton(uiText("Refresh Status", "刷新状态"), hotspot_group);
  enable_button_ =
      new QPushButton(uiText("Enable Hotspot", "开启热点"), hotspot_group);
  disable_button_ =
      new QPushButton(uiText("Disable Hotspot", "关闭热点"), hotspot_group);
  enable_button_->setMinimumWidth(130);
  disable_button_->setMinimumWidth(130);
  actions->addWidget(refresh_button_);
  actions->addWidget(enable_button_);
  actions->addWidget(disable_button_);
  actions->addStretch(1);
  group_layout->addLayout(actions);

  root->addWidget(hotspot_group);
  root->addStretch(1);

  connect(refresh_button_, &QPushButton::clicked, this, [this]() {
    if (on_refresh) on_refresh();
  });
  connect(enable_button_, &QPushButton::clicked, this, [this]() {
    if (on_enable) on_enable();
  });
  connect(disable_button_, &QPushButton::clicked, this, [this]() {
    if (on_disable) on_disable();
  });
  clear();
}

void WifiHotspotPanel::clear() {
  status_ = {};
  has_status_ = false;
  controls_locked_ = false;
  busy_ = false;
  busy_message_.clear();
  operation_error_.clear();
  refreshView();
}

void WifiHotspotPanel::setDeviceOpen(bool open) {
  device_open_ = open;
  if (!open) clear();
  refreshView();
}

void WifiHotspotPanel::setControlsLocked(bool locked) {
  controls_locked_ = locked;
  refreshView();
}

void WifiHotspotPanel::setBusy(bool busy, const QString& message) {
  busy_ = busy;
  busy_message_ = message;
  if (busy) operation_error_.clear();
  refreshView();
}

void WifiHotspotPanel::setStatus(const WifiHotspotViewState& status) {
  status_ = status;
  has_status_ = true;
  operation_error_.clear();
  refreshView();
}

void WifiHotspotPanel::setError(const QString& error) {
  operation_error_ = error;
  refreshView();
}

void WifiHotspotPanel::setMessage(const QString& text, bool warning,
                                  bool error) {
  message_label_->setText(text);
  if (error) {
    message_label_->setStyleSheet(QStringLiteral(
        "background: #fef3f2; color: #b42318; border: 1px solid #fecdca;"
        "border-radius: 6px; padding: 8px 10px; font-weight: 600;"));
  } else if (warning) {
    message_label_->setStyleSheet(QStringLiteral(
        "background: #fffaeb; color: #b54708; border: 1px solid #fedf89;"
        "border-radius: 6px; padding: 8px 10px; font-weight: 600;"));
  } else {
    message_label_->setStyleSheet(QStringLiteral(
        "background: #ecfdf3; color: #027a48; border: 1px solid #abefc6;"
        "border-radius: 6px; padding: 8px 10px; font-weight: 600;"));
  }
}

void WifiHotspotPanel::refreshView() {
  detection_value_->setText(
      !has_status_
          ? QStringLiteral("-")
          : (status_.present ? uiText("Detected", "已检测到")
                             : uiText("Not detected", "未检测到")));
  interface_value_->setText(valueOrDash(status_.interface_name));

  QString state = QStringLiteral("-");
  if (has_status_) {
    if (!status_.present) {
      state = uiText("Unavailable", "不可用");
    } else if (status_.running) {
      state = uiText("Running", "运行中");
    } else if (status_.ap_running || status_.dhcp_running) {
      state = uiText("Partially running", "部分运行");
    } else if (status_.enabled) {
      state = uiText("Enabled, not running", "已启用，尚未运行");
    } else {
      state = uiText("Disabled", "已关闭");
    }
  }
  state_value_->setText(state);
  ssid_value_->setText(valueOrDash(status_.ssid));
  security_value_->setText(
      uiText("Open network (no password)", "开放网络（无密码）"));
  QString address = status_.address.isEmpty()
                        ? QStringLiteral("10.42.200.1")
                        : status_.address;
  if (!address.contains(QChar('/'))) address += QStringLiteral("/24");
  address_value_->setText(address);
  dhcp_value_->setText(
      !has_status_
          ? QStringLiteral("-")
          : (status_.dhcp_running ? uiText("Running", "运行中")
                                  : uiText("Stopped", "已停止")));
  if (!has_status_) {
    persisted_value_->setText(QStringLiteral("-"));
  } else if (!status_.persisted) {
    persisted_value_->setText(
        uiText("Not saved / default", "未保存 / 使用默认值"));
  } else {
    persisted_value_->setText(
        status_.enabled ? uiText("Enabled", "已启用")
                        : uiText("Disabled", "已关闭"));
  }

  if (!device_open_) {
    setMessage(uiText("Open a device to manage its Wi-Fi hotspot",
                      "打开设备后可管理 Wi-Fi 热点"),
               true, false);
  } else if (busy_) {
    setMessage(
        busy_message_.isEmpty()
            ? uiText("Wi-Fi hotspot operation in progress...",
                     "正在执行 Wi-Fi 热点操作…")
            : busy_message_,
        false, false);
  } else if (!operation_error_.isEmpty()) {
    setMessage(
        uiText("Wi-Fi hotspot operation failed: %1",
               "Wi-Fi 热点操作失败：%1")
            .arg(operation_error_),
        false, true);
  } else if (!has_status_) {
    setMessage(uiText("Wi-Fi hotspot status has not been read",
                      "尚未读取 Wi-Fi 热点状态"),
               true, false);
  } else if (!status_.present) {
    setMessage(uiText("No Wi-Fi adapter was detected on the RK device",
                      "RK 设备上未检测到 Wi-Fi 设备"),
               true, false);
  } else if (status_.error_code != 0 || !status_.error.isEmpty()) {
    const QString detail =
        status_.error.isEmpty()
            ? uiText("error code %1", "错误码 %1").arg(status_.error_code)
            : status_.error;
    setMessage(
        uiText("Wi-Fi hotspot reported an error: %1",
               "Wi-Fi 热点报告错误：%1")
            .arg(detail),
        false, true);
  } else if (status_.running) {
    setMessage(uiText("Wi-Fi hotspot and DHCP are running",
                      "Wi-Fi 热点和 DHCP 正在运行"),
               false, false);
  } else {
    setMessage(uiText("Wi-Fi hotspot is disabled",
                      "Wi-Fi 热点已关闭"),
               false, false);
  }

  const bool can_interact = device_open_ && !busy_ && !controls_locked_;
  refresh_button_->setEnabled(can_interact);
  enable_button_->setEnabled(
      can_interact && has_status_ && !status_.enabled);
  disable_button_->setEnabled(
      can_interact && has_status_ &&
      (status_.enabled || status_.ap_running || status_.dhcp_running));
}

}  // namespace prism_viewer::ui
