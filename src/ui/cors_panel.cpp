#include "ui/cors_panel.hpp"

#include "common/ui_text.hpp"

#include <QtCore/QSettings>
#include <QtGui/QDoubleValidator>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QVBoxLayout>

namespace prism_viewer::ui {
namespace {

using prism_viewer::common::uiText;

QString byteCount(uint64_t bytes) {
  if (bytes < 1024u) return QStringLiteral("%1 B").arg(bytes);
  if (bytes < 1024u * 1024u) {
    return QStringLiteral("%1 KiB")
        .arg(static_cast<double>(bytes) / 1024.0, 0, 'f', 1);
  }
  return QStringLiteral("%1 MiB")
      .arg(static_cast<double>(bytes) / (1024.0 * 1024.0), 0, 'f', 2);
}

bool sessionInProgress(cors::CorsSessionPhase phase) {
  return phase != cors::CorsSessionPhase::Disconnected &&
         phase != cors::CorsSessionPhase::Error;
}

}  // namespace

CorsPanel::CorsPanel(QWidget* parent) : QWidget(parent) {
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(8, 8, 8, 8);
  root->setSpacing(10);

  auto* config_group =
      new QGroupBox(uiText("CORS / NTRIP Configuration",
                           "CORS / NTRIP 配置"),
                    this);
  auto* config_layout = new QVBoxLayout(config_group);
  auto* form = new QFormLayout();
  form->setHorizontalSpacing(18);
  form->setVerticalSpacing(8);

  provider_selector_ = new QComboBox(config_group);
  provider_selector_->setObjectName(QStringLiteral("corsServiceProvider"));
  endpoint_selector_ = new QComboBox(config_group);
  endpoint_selector_->setObjectName(QStringLiteral("corsEndpointPolicy"));
  coordinate_system_selector_ = new QComboBox(config_group);
  coordinate_system_selector_->setObjectName(
      QStringLiteral("corsCoordinateSystem"));
  coordinate_system_selector_->addItem(
      QStringLiteral("WGS84 — 8002"), 8002);
  coordinate_system_selector_->addItem(
      QStringLiteral("CGCS2000 — 8001"), 8001);
  mountpoint_selector_ = new QComboBox(config_group);
  mountpoint_selector_->setObjectName(QStringLiteral("corsMountpoint"));
  username_edit_ = new QLineEdit(config_group);
  username_edit_->setObjectName(QStringLiteral("corsUsername"));
  password_edit_ = new QLineEdit(config_group);
  password_edit_->setObjectName(QStringLiteral("corsPassword"));
  password_edit_->setEchoMode(QLineEdit::Password);
  remember_password_checkbox_ =
      new QCheckBox(uiText(
          "Remember password in this computer's local settings",
          "在本机设置中记住密码"),
                    config_group);

  latitude_edit_ = new QLineEdit(config_group);
  latitude_edit_->setObjectName(QStringLiteral("corsLatitude"));
  latitude_edit_->setPlaceholderText(QStringLiteral("31.230400"));
  latitude_edit_->setValidator(
      new QDoubleValidator(-90.0, 90.0, 8, latitude_edit_));
  longitude_edit_ = new QLineEdit(config_group);
  longitude_edit_->setObjectName(QStringLiteral("corsLongitude"));
  longitude_edit_->setPlaceholderText(QStringLiteral("121.473700"));
  longitude_edit_->setValidator(
      new QDoubleValidator(-180.0, 180.0, 8, longitude_edit_));
  altitude_edit_ = new QLineEdit(config_group);
  altitude_edit_->setObjectName(QStringLiteral("corsAltitude"));
  altitude_edit_->setText(QStringLiteral("0"));
  altitude_edit_->setValidator(
      new QDoubleValidator(-1000.0, 20000.0, 3, altitude_edit_));

  form->addRow(uiText("Service provider:", "服务商："),
               provider_selector_);
  form->addRow(uiText("Caster endpoint:", "Caster 地址："),
               endpoint_selector_);
  form->addRow(uiText("Coordinate system:", "坐标系："),
               coordinate_system_selector_);
  form->addRow(uiText("Mountpoint:", "挂载点："),
               mountpoint_selector_);
  form->addRow(uiText("Username:", "账号："), username_edit_);
  form->addRow(uiText("Password:", "密码："), password_edit_);
  form->addRow(QString(), remember_password_checkbox_);
  form->addRow(uiText("Rover latitude (deg):", "流动站纬度（度）："),
               latitude_edit_);
  form->addRow(uiText("Rover longitude (deg):", "流动站经度（度）："),
               longitude_edit_);
  form->addRow(uiText("Rover altitude (m):", "流动站高程（米）："),
               altitude_edit_);
  config_layout->addLayout(form);

  auto* position_note = new QLabel(
      uiText("The caster needs an approximate rover position in GGA. "
             "Enter the current position; a future navigation-data API can "
             "replace this manual source.",
             "Caster 需要 GGA 中的流动站概略位置。请填写当前位置；后续可用"
             "导航数据接口替代手工位置。"),
      config_group);
  position_note->setWordWrap(true);
  position_note->setStyleSheet(
      QStringLiteral("color: #475467; padding: 2px 0;"));
  config_layout->addWidget(position_note);

  auto* actions = new QHBoxLayout();
  connect_button_ =
      new QPushButton(uiText("Connect CORS", "连接 CORS"), config_group);
  disconnect_button_ =
      new QPushButton(uiText("Disconnect", "断开连接"), config_group);
  actions->addWidget(connect_button_);
  actions->addWidget(disconnect_button_);
  actions->addStretch(1);
  config_layout->addLayout(actions);

  auto* status_group =
      new QGroupBox(uiText("CORS / RTK Status", "CORS / RTK 状态"), this);
  auto* status_layout = new QVBoxLayout(status_group);
  message_label_ = new QLabel(status_group);
  message_label_->setWordWrap(true);
  status_layout->addWidget(message_label_);
  auto* status_form = new QFormLayout();
  endpoint_value_ = new QLabel(QStringLiteral("-"), status_group);
  received_value_ = new QLabel(QStringLiteral("-"), status_group);
  forwarded_value_ = new QLabel(QStringLiteral("-"), status_group);
  source_value_ = new QLabel(QStringLiteral("-"), status_group);
  solution_value_ = new QLabel(QStringLiteral("-"), status_group);
  agent_value_ = new QLabel(QStringLiteral("-"), status_group);
  endpoint_value_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  status_form->addRow(uiText("Active endpoint:", "当前地址："),
                      endpoint_value_);
  status_form->addRow(uiText("NTRIP received:", "NTRIP 已接收："),
                      received_value_);
  status_form->addRow(uiText("Forwarded to RK:", "已转发至 RK："),
                      forwarded_value_);
  status_form->addRow(uiText("Agent base source:", "Agent 基站源："),
                      source_value_);
  status_form->addRow(uiText("RTK solution:", "RTK 解状态："),
                      solution_value_);
  status_form->addRow(uiText("Agent counters:", "Agent 计数："),
                      agent_value_);
  status_layout->addLayout(status_form);

  root->addWidget(config_group);
  root->addWidget(status_group);
  root->addStretch(1);

  populateProviders();
  loadSettings();
  connect(provider_selector_,
          QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [this](int) { populateProviderOptions(); });
  connect(connect_button_, &QPushButton::clicked, this, [this]() {
    QString error;
    const cors::CorsConfiguration requested = configuration(&error);
    if (!error.isEmpty()) {
      status_.phase = cors::CorsSessionPhase::Error;
      status_.error = error;
      refreshView();
      return;
    }
    saveSettings();
    if (on_connect) on_connect(requested);
  });
  connect(disconnect_button_, &QPushButton::clicked, this, [this]() {
    if (on_disconnect) on_disconnect();
  });
  refreshView();
}

void CorsPanel::populateProviders() {
  provider_selector_->clear();
  for (const auto& provider : cors::corsServiceProviders()) {
    provider_selector_->addItem(provider.display_name, provider.id);
  }
  populateProviderOptions();
}

void CorsPanel::populateProviderOptions() {
  const QString previous_mount = mountpoint_selector_->currentData().toString();
  endpoint_selector_->clear();
  mountpoint_selector_->clear();
  const auto* provider = cors::findCorsServiceProvider(
      provider_selector_->currentData().toString());
  if (provider == nullptr || provider->endpoints.isEmpty()) return;
  const QString primary = provider->endpoints.front().host;
  const QString backup = provider->endpoints.size() > 1
                             ? provider->endpoints.at(1).host
                             : primary;
  endpoint_selector_->addItem(
      uiText("Automatic (%1, then %2)", "自动（%1，失败后 %2）")
          .arg(primary, backup),
      QStringLiteral("automatic"));
  endpoint_selector_->addItem(
      uiText("Primary only — %1", "仅主服务 — %1").arg(primary),
      QStringLiteral("primary"));
  endpoint_selector_->addItem(
      uiText("Backup only — %1", "仅备用服务 — %1").arg(backup),
      QStringLiteral("backup"));
  for (const auto& mountpoint : provider->mountpoints) {
    mountpoint_selector_->addItem(mountpoint.display_name, mountpoint.id);
  }
  const int previous_index = mountpoint_selector_->findData(previous_mount);
  if (previous_index >= 0) mountpoint_selector_->setCurrentIndex(previous_index);
}

void CorsPanel::loadSettings() {
  QSettings settings(QStringLiteral("DIBULI"), QStringLiteral("PrismViewer"));
  settings.beginGroup(QStringLiteral("cors"));
  const QString provider =
      settings.value(QStringLiteral("serviceProvider"),
                     QStringLiteral("china_mobile"))
          .toString();
  int index = provider_selector_->findData(provider);
  if (index >= 0) provider_selector_->setCurrentIndex(index);
  populateProviderOptions();
  index = endpoint_selector_->findData(
      settings.value(QStringLiteral("endpointPolicy"),
                     QStringLiteral("automatic")));
  if (index >= 0) endpoint_selector_->setCurrentIndex(index);
  index = coordinate_system_selector_->findData(
      settings.value(QStringLiteral("port"), 8002));
  if (index >= 0) coordinate_system_selector_->setCurrentIndex(index);
  index = mountpoint_selector_->findData(
      settings.value(QStringLiteral("mountpoint"),
                     QStringLiteral("RTCM33_GRCEJ")));
  if (index >= 0) mountpoint_selector_->setCurrentIndex(index);
  username_edit_->setText(
      settings.value(QStringLiteral("username")).toString());
  const bool remember =
      settings.value(QStringLiteral("rememberPassword"), false).toBool();
  remember_password_checkbox_->setChecked(remember);
  if (remember) {
    password_edit_->setText(
        settings.value(QStringLiteral("password")).toString());
  }
  latitude_edit_->setText(
      settings.value(QStringLiteral("latitude")).toString());
  longitude_edit_->setText(
      settings.value(QStringLiteral("longitude")).toString());
  altitude_edit_->setText(
      settings.value(QStringLiteral("altitude"), QStringLiteral("0"))
          .toString());
  settings.endGroup();
}

void CorsPanel::saveSettings() {
  QSettings settings(QStringLiteral("DIBULI"), QStringLiteral("PrismViewer"));
  settings.beginGroup(QStringLiteral("cors"));
  settings.setValue(QStringLiteral("serviceProvider"),
                    provider_selector_->currentData());
  settings.setValue(QStringLiteral("endpointPolicy"),
                    endpoint_selector_->currentData());
  settings.setValue(QStringLiteral("port"),
                    coordinate_system_selector_->currentData());
  settings.setValue(QStringLiteral("mountpoint"),
                    mountpoint_selector_->currentData());
  settings.setValue(QStringLiteral("username"), username_edit_->text());
  settings.setValue(QStringLiteral("latitude"), latitude_edit_->text());
  settings.setValue(QStringLiteral("longitude"), longitude_edit_->text());
  settings.setValue(QStringLiteral("altitude"), altitude_edit_->text());
  const bool remember = remember_password_checkbox_->isChecked();
  settings.setValue(QStringLiteral("rememberPassword"), remember);
  if (remember) {
    settings.setValue(QStringLiteral("password"), password_edit_->text());
  } else {
    settings.remove(QStringLiteral("password"));
  }
  settings.endGroup();
}

cors::CorsConfiguration CorsPanel::configuration(QString* error) const {
  cors::CorsConfiguration result;
  result.service_provider = provider_selector_->currentData().toString();
  const auto* provider =
      cors::findCorsServiceProvider(result.service_provider);
  if (provider != nullptr) {
    const QString endpoint_policy =
        endpoint_selector_->currentData().toString();
    if (endpoint_policy == QStringLiteral("primary")) {
      if (!provider->endpoints.isEmpty()) {
        result.endpoints.push_back(provider->endpoints.front());
      }
    } else if (endpoint_policy == QStringLiteral("backup")) {
      if (provider->endpoints.size() > 1) {
        result.endpoints.push_back(provider->endpoints.at(1));
      }
    } else {
      result.endpoints = provider->endpoints;
    }
    const quint16 port = static_cast<quint16>(
        coordinate_system_selector_->currentData().toUInt());
    for (auto& endpoint : result.endpoints) endpoint.port = port;
  }
  result.mountpoint = mountpoint_selector_->currentData().toString();
  result.username = username_edit_->text();
  result.password = password_edit_->text();
  bool latitude_ok = false;
  bool longitude_ok = false;
  bool altitude_ok = false;
  result.latitude_degrees = latitude_edit_->text().toDouble(&latitude_ok);
  result.longitude_degrees = longitude_edit_->text().toDouble(&longitude_ok);
  result.altitude_meters = altitude_edit_->text().toDouble(&altitude_ok);
  QString validation;
  if (!latitude_ok || !longitude_ok || !altitude_ok) {
    validation = uiText(
        "Enter valid rover latitude, longitude and altitude values",
        "请输入有效的流动站纬度、经度和高程");
  } else {
    validation = cors::validateCorsConfiguration(result);
  }
  if (error != nullptr) *error = validation;
  return result;
}

void CorsPanel::setDeviceOpen(bool open) {
  device_open_ = open;
  refreshView();
}

void CorsPanel::setSessionStatus(const cors::CorsSessionStatus& status) {
  status_ = status;
  refreshView();
}

void CorsPanel::refreshView() {
  const bool active = sessionInProgress(status_.phase);
  provider_selector_->setEnabled(device_open_ && !active);
  endpoint_selector_->setEnabled(device_open_ && !active);
  coordinate_system_selector_->setEnabled(device_open_ && !active);
  mountpoint_selector_->setEnabled(device_open_ && !active);
  username_edit_->setEnabled(device_open_ && !active);
  password_edit_->setEnabled(device_open_ && !active);
  remember_password_checkbox_->setEnabled(device_open_ && !active);
  latitude_edit_->setEnabled(device_open_ && !active);
  longitude_edit_->setEnabled(device_open_ && !active);
  altitude_edit_->setEnabled(device_open_ && !active);
  connect_button_->setEnabled(device_open_ && !active);
  disconnect_button_->setEnabled(active);

  endpoint_value_->setText(
      status_.endpoint.isEmpty() ? QStringLiteral("-") : status_.endpoint);
  received_value_->setText(byteCount(status_.received_bytes));
  forwarded_value_->setText(byteCount(status_.forwarded_bytes));
  if (status_.rtk_status_valid) {
    source_value_->setText(QString::fromLatin1(
        communication::rtkBaseSourceName(status_.rtk_status.base_source)));
    solution_value_->setText(QString::fromLatin1(
        communication::rtkSolutionName(status_.rtk_status.solution)));
    agent_value_->setText(
        QStringLiteral("RTCM=%1 epochs=%2 fix=%3 float=%4 errors=%5")
            .arg(status_.rtk_status.base_rtcm_messages)
            .arg(status_.rtk_status.base_observation_epochs)
            .arg(status_.rtk_status.fix_count)
            .arg(status_.rtk_status.float_count)
            .arg(status_.rtk_status.decoder_errors));
  } else {
    source_value_->setText(QStringLiteral("-"));
    solution_value_->setText(QStringLiteral("-"));
    agent_value_->setText(QStringLiteral("-"));
  }

  QString message;
  bool error = false;
  bool warning = false;
  if (!device_open_) {
    message = uiText("Open a device before connecting CORS",
                     "请先打开设备，再连接 CORS");
    warning = true;
  } else if (status_.phase == cors::CorsSessionPhase::Error) {
    message = uiText("CORS failed: %1", "CORS 失败：%1").arg(status_.error);
    error = true;
  } else if (status_.phase == cors::CorsSessionPhase::Disconnected) {
    message = uiText("CORS is disconnected", "CORS 未连接");
    warning = true;
  } else if (status_.phase == cors::CorsSessionPhase::Streaming) {
    message = uiText(
        "Receiving RTCM and forwarding it to the RK Agent",
        "正在接收 RTCM 并转发给 RK Agent");
  } else if (status_.phase == cors::CorsSessionPhase::Reconnecting) {
    message = uiText("CORS reconnecting: %1", "CORS 正在重连：%1")
                  .arg(status_.error);
    warning = true;
  } else {
    message = uiText("CORS session: %1", "CORS 会话：%1")
                  .arg(cors::corsSessionPhaseName(status_.phase));
  }
  const QString style =
      error
          ? QStringLiteral(
                "background:#fef3f2;color:#b42318;border:1px solid #fecdca;"
                "border-radius:6px;padding:8px 10px;font-weight:600;")
          : warning
                ? QStringLiteral(
                      "background:#fffaeb;color:#b54708;border:1px solid #fedf89;"
                      "border-radius:6px;padding:8px 10px;font-weight:600;")
                : QStringLiteral(
                      "background:#ecfdf3;color:#027a48;border:1px solid #abefc6;"
                      "border-radius:6px;padding:8px 10px;font-weight:600;");
  message_label_->setText(message);
  message_label_->setStyleSheet(style);
}

}  // namespace prism_viewer::ui
