#include "ui/cors_panel.hpp"

#include "common/ui_text.hpp"
#include "ui/rtk_error_help.hpp"

#include <QtCore/QSettings>
#include <QtCore/QDateTime>
#include <QtCore/QSignalBlocker>
#include <QtCore/QTime>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QTextBrowser>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QSizePolicy>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <utility>

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

QString nmeaFixQualityName(uint8_t quality) {
  switch (quality) {
    case 0: return uiText("invalid", "无效");
    case 1: return uiText("single", "单点定位");
    case 2: return QStringLiteral("DGPS");
    case 4: return uiText("RTK fixed", "RTK 固定解");
    case 5: return uiText("RTK float", "RTK 浮点解");
    case 6: return uiText("dead reckoning", "航位推算");
    case 7: return uiText("manual", "手工输入");
    case 8: return uiText("simulation", "仿真");
    case 9: return QStringLiteral("WAAS");
    default: return uiText("quality %1", "质量码 %1").arg(quality);
  }
}

QString nmeaFixModeName(uint8_t mode) {
  switch (mode) {
    case 1: return uiText("no fix", "无定位");
    case 2: return QStringLiteral("2D");
    case 3: return QStringLiteral("3D");
    default: return QStringLiteral("-");
  }
}


struct CartesianPosition {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
};

CartesianPosition geodeticToEcef(double latitude_degrees,
                                 double longitude_degrees,
                                 double ellipsoidal_height_meters) {
  constexpr double kPi = 3.14159265358979323846;
  constexpr double kWgs84SemiMajorAxisMeters = 6378137.0;
  constexpr double kWgs84FirstEccentricitySquared =
      6.6943799901413165e-3;
  const double latitude = latitude_degrees * kPi / 180.0;
  const double longitude = longitude_degrees * kPi / 180.0;
  const double sin_latitude = std::sin(latitude);
  const double cos_latitude = std::cos(latitude);
  const double sin_longitude = std::sin(longitude);
  const double cos_longitude = std::cos(longitude);
  const double prime_vertical_radius =
      kWgs84SemiMajorAxisMeters /
      std::sqrt(1.0 - kWgs84FirstEccentricitySquared *
                          sin_latitude * sin_latitude);
  return {
      (prime_vertical_radius + ellipsoidal_height_meters) *
          cos_latitude * cos_longitude,
      (prime_vertical_radius + ellipsoidal_height_meters) *
          cos_latitude * sin_longitude,
      (prime_vertical_radius *
               (1.0 - kWgs84FirstEccentricitySquared) +
           ellipsoidal_height_meters) *
          sin_latitude,
  };
}

CartesianPosition geodeticToEnu(double latitude_degrees,
                                double longitude_degrees,
                                double ellipsoidal_height_meters,
                                double origin_latitude_degrees,
                                double origin_longitude_degrees,
                                double origin_height_meters) {
  constexpr double kPi = 3.14159265358979323846;
  const CartesianPosition position = geodeticToEcef(
      latitude_degrees, longitude_degrees, ellipsoidal_height_meters);
  const CartesianPosition origin = geodeticToEcef(
      origin_latitude_degrees, origin_longitude_degrees,
      origin_height_meters);
  const double latitude = origin_latitude_degrees * kPi / 180.0;
  const double longitude = origin_longitude_degrees * kPi / 180.0;
  const double sin_latitude = std::sin(latitude);
  const double cos_latitude = std::cos(latitude);
  const double sin_longitude = std::sin(longitude);
  const double cos_longitude = std::cos(longitude);
  const double dx = position.x - origin.x;
  const double dy = position.y - origin.y;
  const double dz = position.z - origin.z;
  return {
      -sin_longitude * dx + cos_longitude * dy,
      -sin_latitude * cos_longitude * dx -
          sin_latitude * sin_longitude * dy + cos_latitude * dz,
      cos_latitude * cos_longitude * dx +
          cos_latitude * sin_longitude * dy + sin_latitude * dz,
  };
}

void addStatusField(QVBoxLayout* layout, QWidget* parent,
                    const QString& caption, QLabel* value) {
  auto* row = new QWidget(parent);
  auto* row_layout = new QVBoxLayout(row);
  row_layout->setContentsMargins(0, 0, 0, 4);
  row_layout->setSpacing(2);
  auto* caption_label = new QLabel(caption, row);
  caption_label->setStyleSheet(
      QStringLiteral("color:#475467;font-weight:600;"));
  value->setParent(row);
  value->setWordWrap(true);
  value->setAlignment(Qt::AlignLeft | Qt::AlignTop);
  value->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  value->setTextInteractionFlags(Qt::TextSelectableByMouse);
  row_layout->addWidget(caption_label);
  row_layout->addWidget(value);
  layout->addWidget(row);
}

}  // namespace

CorsPanel::CorsPanel(QWidget* parent) : QWidget(parent) {
  auto* root = new QHBoxLayout(this);
  root->setContentsMargins(8, 8, 8, 8);
  root->setSpacing(10);

  auto* config_group =
      new QGroupBox(uiText("CORS / NTRIP Configuration",
                           "CORS / NTRIP 配置"),
                    this);
  config_group->setObjectName(QStringLiteral("corsConfigurationColumn"));
  auto* config_layout = new QVBoxLayout(config_group);
  auto* form = new QFormLayout();
  form->setHorizontalSpacing(18);
  form->setVerticalSpacing(8);

  provider_selector_ = new QComboBox(config_group);
  provider_selector_->setObjectName(QStringLiteral("corsServiceProvider"));
  endpoint_selector_ = new QComboBox(config_group);
  endpoint_selector_->setObjectName(QStringLiteral("corsEndpointPolicy"));
  endpoint_selector_->setEditable(true);
  endpoint_selector_->setInsertPolicy(QComboBox::NoInsert);
  endpoint_selector_->lineEdit()->setPlaceholderText(
      uiText("IP, hostname or URL", "IP、域名或 URL"));
  endpoint_selector_->setToolTip(
      uiText("Select a preset or type an IP address, hostname, host:port, "
             "or http[s]/ntrip[s] URL. A URL path overrides the mountpoint.",
             "可选择预设，或输入 IP、域名、主机:端口、http[s]/ntrip[s] URL。"
             "URL 路径会覆盖挂载点。"));
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
  config_layout->addLayout(form);

  auto* position_note = new QLabel(
      uiText("Rover position, UTC, fix quality, satellites, HDOP, altitude "
             "and geoid separation are read from the device GNSS. A fresh "
             "valid fix is required; GGA is sent to the caster every second.",
             "流动站位置、UTC、定位质量、卫星数、HDOP、高程和大地水准面差"
             "均从设备 GNSS 自动读取。连接需要有效且新鲜的定位；GGA 每秒发送"
             "一次。"),
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
  status_group->setObjectName(QStringLiteral("corsRtkStatusColumn"));
  auto* status_layout = new QVBoxLayout(status_group);
  message_label_ = new QLabel(status_group);
  message_label_->setWordWrap(true);
  status_layout->addWidget(message_label_);
  endpoint_value_ = new QLabel(QStringLiteral("-"), status_group);
  received_value_ = new QLabel(QStringLiteral("-"), status_group);
  forwarded_value_ = new QLabel(QStringLiteral("-"), status_group);
  source_value_ = new QLabel(QStringLiteral("-"), status_group);
  solution_value_ = new QLabel(QStringLiteral("-"), status_group);
  gnss_receiver_value_ = new QLabel(QStringLiteral("-"), this);
  gnss_receiver_value_->setObjectName(QStringLiteral("gnssReceiverQuality"));
  gnss_reception_value_ = new QLabel(QStringLiteral("-"), this);
  gnss_reception_value_->setObjectName(QStringLiteral("gnssReceptionStatus"));
  gnss_reception_value_->setWordWrap(true);
  gnss_sync_value_ = new QLabel(QStringLiteral("-"), this);
  gnss_sync_value_->setObjectName(QStringLiteral("gnssTimeSyncStatus"));
  gnss_sync_value_->setWordWrap(true);
  gnss_dop_value_ = new QLabel(QStringLiteral("-"), this);
  gnss_dop_value_->setObjectName(QStringLiteral("gnssDop"));
  local_origin_value_ = new QLabel(QStringLiteral("-"), this);
  local_origin_value_->setObjectName(QStringLiteral("localEnuOrigin"));
  gnss_position_value_ = new QLabel(QStringLiteral("-"), this);
  gnss_position_value_->setObjectName(QStringLiteral("gnssPosition"));
  gnss_utc_value_ = new QLabel(QStringLiteral("-"), this);
  gnss_utc_value_->setObjectName(QStringLiteral("gnssUtc"));
  gnss_timing_value_ = new QLabel(QStringLiteral("-"), this);
  gnss_timing_value_->setObjectName(QStringLiteral("gnssTimingQuality"));
  endpoint_value_->setWordWrap(true);
  source_value_->setWordWrap(true);
  solution_value_->setWordWrap(true);
  agent_value_ = new QLabel(QStringLiteral("-"), status_group);
  agent_value_->setObjectName(QStringLiteral("rtkAgentCounters"));
  agent_value_->setWordWrap(true);
  gnss_receiver_value_->setWordWrap(true);
  gnss_dop_value_->setWordWrap(true);
  local_origin_value_->setWordWrap(true);
  gnss_position_value_->setWordWrap(true);
  gnss_utc_value_->setWordWrap(true);
  gnss_timing_value_->setWordWrap(true);
  endpoint_value_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  addStatusField(status_layout, status_group,
                 uiText("Active endpoint", "当前地址"), endpoint_value_);
  addStatusField(status_layout, status_group,
                 uiText("NTRIP received", "NTRIP 已接收"),
                 received_value_);
  addStatusField(status_layout, status_group,
                 uiText("Forwarded to RK", "已转发至 RK"), forwarded_value_);
  addStatusField(status_layout, status_group,
                 uiText("Agent base source", "Agent 基站源"), source_value_);
  addStatusField(status_layout, status_group,
                 uiText("RTK solution", "RTK 解状态"),
                 solution_value_);
  addStatusField(status_layout, status_group,
                 uiText("Agent counters", "Agent 计数"), agent_value_);
  status_layout->addStretch(1);

  auto* gnss_group =
      new QGroupBox(uiText("GPS / RTK Position", "GPS / RTK 位置"), this);
  gnss_group->setObjectName(QStringLiteral("gpsGnssStatusColumn"));
  auto* gnss_group_layout = new QVBoxLayout(gnss_group);
  gnss_group_layout->setContentsMargins(4, 8, 4, 4);
  auto* gnss_tabs = new QTabWidget(gnss_group);
  gnss_tabs->setObjectName(QStringLiteral("gnssRtkTabs"));

  auto make_scroll_tab = [&](const QString& page_name,
                             const QString& scroll_name,
                             const QString& title) {
    auto* page = new QWidget(gnss_tabs);
    page->setObjectName(page_name);
    auto* page_layout = new QVBoxLayout(page);
    page_layout->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea(page);
    scroll->setObjectName(scroll_name);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setStyleSheet(QStringLiteral(
        "QScrollArea { background: transparent; border: none; }"));
    auto* content = new QWidget(scroll);
    auto* content_layout = new QVBoxLayout(content);
    content_layout->setContentsMargins(8, 8, 8, 8);
    content_layout->setSpacing(8);
    content_layout->setSizeConstraint(QLayout::SetMinimumSize);
    scroll->setWidget(content);
    page_layout->addWidget(scroll);
    gnss_tabs->addTab(page, title);
    return std::pair<QWidget*, QVBoxLayout*>(content, content_layout);
  };
  const auto gps_tab = make_scroll_tab(
      QStringLiteral("gpsStatusTab"), QStringLiteral("gpsStatusScroll"),
      uiText("GPS / GNSS", "GPS / GNSS"));
  QWidget* const gps_content = gps_tab.first;
  QVBoxLayout* const gps_layout = gps_tab.second;
  auto* gnss_heading = new QLabel(
      uiText("Live receiver and position at 10 Hz; basic PPS status at 1 Hz",
             "接收机与位置按 10 Hz 刷新；基础 PPS 状态按 1 Hz 更新"),
      gps_content);
  gnss_heading->setWordWrap(true);
  gnss_heading->setStyleSheet(
      QStringLiteral("color:#475467;padding:2px 0 6px 0;"));
  gps_layout->addWidget(gnss_heading);

  auto* timesync_group = new QGroupBox(uiText("Timesync port mode", "Timesync 接口模式"), gps_content);
  auto* timesync_layout = new QVBoxLayout(timesync_group);
  auto* timesync_hint = new QLabel(uiText(
      "Saved on RK; restored after reboot. Stop capture before switching. RTK mode does not start CORS. Status is a manually refreshed snapshot.",
      "保存在 RK 本地，重启后恢复。停止采集后切换；进入 RTK 模式不启动 CORS。状态为手动刷新时的快照。"), timesync_group);
  timesync_hint->setWordWrap(true);
  timesync_layout->addWidget(timesync_hint);
  auto* timesync_actions = new QHBoxLayout();
  timesync_mode_ = new QComboBox(timesync_group);
  timesync_mode_->setObjectName(QStringLiteral("timesyncMode"));
  timesync_mode_->addItem(uiText("GNSS / PPS + NMEA input", "GNSS / PPS + NMEA 输入"), 0);
  timesync_mode_->addItem(uiText("PPS + NMEA output", "PPS + NMEA 输出"), 1);
  timesync_mode_->addItem(uiText("RTK mode", "RTK 模式"), 2);
  timesync_refresh_ = new QPushButton(uiText("Read status", "读取实际状态"), timesync_group);
  timesync_apply_ = new QPushButton(uiText("Apply mode", "应用模式"), timesync_group);
  timesync_refresh_->setObjectName(QStringLiteral("timesyncRefresh"));
  timesync_apply_->setObjectName(QStringLiteral("timesyncApply"));
  timesync_actions->addWidget(timesync_mode_, 1);
  timesync_actions->addWidget(timesync_refresh_);
  timesync_actions->addWidget(timesync_apply_);
  timesync_layout->addLayout(timesync_actions);
  timesync_status_ = new QLabel(uiText("Not read", "尚未读取"), timesync_group);
  timesync_status_->setObjectName(QStringLiteral("timesyncStatus"));
  timesync_status_->setWordWrap(true);
  timesync_layout->addWidget(timesync_status_);
  module_versions_ = new QLabel(timesync_group);
  module_versions_->setObjectName(QStringLiteral("rtkModuleVersions"));
  module_versions_->setWordWrap(true);
  module_versions_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  timesync_layout->addWidget(module_versions_);
  setRtkModuleVersions(std::nullopt);
  auto* error_help = new QPushButton(uiText("ⓘ LED / error help", "ⓘ LED / 错误说明"), timesync_group);
  error_help->setObjectName(QStringLiteral("rtkErrorHelp"));
  error_help->setToolTip(uiText("RTK-module LED patterns, priority and error explanations; read-only", "RTK-module LED 闪烁规则、优先级和错误说明；只读，不发送命令"));
  timesync_layout->addWidget(error_help, 0, Qt::AlignLeft);
  connect(error_help, &QPushButton::clicked, this, [this] { showRtkErrorHelp(); });
  gps_layout->addWidget(timesync_group);
  connect(timesync_refresh_, &QPushButton::clicked, this, [this] {
    if(on_timesync) on_timesync(std::nullopt);
  });
  connect(timesync_apply_, &QPushButton::clicked, this, [this] {
    if(on_timesync && timesync_known_)
      on_timesync(static_cast<prism::TimeSyncPortMode>(timesync_mode_->currentData().toUInt()));
  });
  connect(timesync_mode_, QOverload<int>::of(&QComboBox::activated), this, [this](int) {
    timesync_dirty_ = true;
  });

  auto* gnss_input_group =
      new QGroupBox(uiText("GNSS Input", "GNSS 输入"), gps_content);
  gnss_input_group->setObjectName(QStringLiteral("gnssInputConfiguration"));
  auto* gnss_input_layout = new QVBoxLayout(gnss_input_group);
  gnss_input_layout->setContentsMargins(10, 12, 10, 10);
  gnss_input_layout->setSpacing(7);
  auto* gnss_baud_row = new QHBoxLayout();
  gnss_baud_row->addWidget(new QLabel(
      uiText("UART RX baud", "UART RX 波特率"), gnss_input_group));
  gnss_baud_row->addStretch(1);
  gnss_baud_combo_ = new QComboBox(gnss_input_group);
  gnss_baud_combo_->setObjectName(QStringLiteral("gnssUartBaudCombo"));
  for (const uint32_t baud : {4800u, 9600u, 19200u, 38400u, 57600u,
                              115200u, 230400u, 460800u, 921600u}) {
    gnss_baud_combo_->addItem(QString::number(baud), baud);
  }
  gnss_baud_combo_->setMinimumWidth(132);
  const int default_baud_index =
      gnss_baud_combo_->findData(prism::kGnssUartDefaultBaud);
  if (default_baud_index >= 0)
    gnss_baud_combo_->setCurrentIndex(default_baud_index);
  gnss_baud_row->addWidget(gnss_baud_combo_);
  gnss_input_layout->addLayout(gnss_baud_row);
  gnss_configuration_message_label_ = new QLabel(
      uiText("Open a device to read the GNSS input baud",
             "请先打开设备以读取 GNSS 输入波特率"),
      gnss_input_group);
  gnss_configuration_message_label_->setObjectName(
      QStringLiteral("gnssConfigurationMessage"));
  gnss_configuration_message_label_->setWordWrap(true);
  gnss_input_layout->addWidget(gnss_configuration_message_label_);
  auto* gnss_actions = new QHBoxLayout();
  gnss_refresh_button_ =
      new QPushButton(uiText("Refresh", "刷新"), gnss_input_group);
  gnss_refresh_button_->setObjectName(
      QStringLiteral("gnssSettingsRefreshButton"));
  gnss_apply_button_ =
      new QPushButton(uiText("Save baud", "保存波特率"), gnss_input_group);
  gnss_apply_button_->setObjectName(
      QStringLiteral("gnssSettingsApplyButton"));
  gnss_actions->addWidget(gnss_refresh_button_);
  gnss_actions->addWidget(gnss_apply_button_);
  gnss_actions->addStretch(1);
  gnss_input_layout->addLayout(gnss_actions);
  gps_layout->addWidget(gnss_input_group);

  gnss_receiver_value_->setParent(gnss_group);
  gnss_reception_value_->setParent(gnss_group);
  gnss_sync_value_->setParent(gnss_group);
  gnss_dop_value_->setParent(gnss_group);
  local_origin_value_->setParent(gnss_group);
  gnss_position_value_->setParent(gnss_group);
  gnss_utc_value_->setParent(gnss_group);
  gnss_timing_value_->setParent(gnss_group);
  addStatusField(gps_layout, gps_content,
                 uiText("GPS fix", "GPS 定位"), gnss_receiver_value_);
  addStatusField(gps_layout, gps_content,
                 uiText("UART / NMEA reception", "UART / NMEA 接收"),
                 gnss_reception_value_);
  addStatusField(gps_layout, gps_content,
                 uiText("External time lock", "外部授时锁定"), gnss_sync_value_);
  addStatusField(gps_layout, gps_content,
                 uiText("Position DOP", "定位 DOP"), gnss_dop_value_);
  addStatusField(gps_layout, gps_content,
                 uiText("Local ENU origin", "本地 ENU 原点"),
                 local_origin_value_);
  addStatusField(gps_layout, gps_content,
                 uiText("GPS position (GGA)", "GPS 位置（GGA）"),
                 gnss_position_value_);
  addStatusField(gps_layout, gps_content,
                 uiText("GPS UTC (GGA)", "GPS UTC（GGA）"),
                 gnss_utc_value_);
  addStatusField(gps_layout, gps_content,
                 uiText("GNSS / PPS status", "GNSS / PPS 状态"),
                 gnss_timing_value_);
  gps_layout->addStretch(1);

  gnss_group_layout->addWidget(gnss_tabs);

  root->addWidget(config_group, 1);
  root->addWidget(status_group, 1);
  root->addWidget(gnss_group, 1);

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
  connect(gnss_refresh_button_, &QPushButton::clicked, this, [this]() {
    if (on_gnss_refresh) on_gnss_refresh();
  });
  connect(gnss_apply_button_, &QPushButton::clicked, this, [this]() {
    if (!on_gnss_apply || !has_device_configuration_) return;
    prism::DeviceConfiguration requested = device_configuration_;
    requested.gnss_uart_baud = gnss_baud_combo_->currentData().toUInt();
    on_gnss_apply(requested);
  });
  connect(gnss_baud_combo_,
          QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [this](int) { refreshView(); });
  refreshView();
  setTimeSyncLocked(true);
}

void CorsPanel::populateProviders() {
  provider_selector_->clear();
  for (const auto& provider : cors::corsServiceProviders()) {
    provider_selector_->addItem(provider.display_name, provider.id);
  }
  populateProviderOptions();
}

void CorsPanel::setTimeSyncLocked(bool locked) {
  timesync_locked_ = locked;
  timesync_mode_->setEnabled(!locked);
  timesync_refresh_->setEnabled(!locked);
  timesync_apply_->setEnabled(!locked && timesync_known_);
  if (!device_open_) setTimeSyncError(uiText("Not connected", "未连接"));
}

void CorsPanel::setTimeSyncError(const QString& error) {
  setRtkModuleVersions(std::nullopt);
  error_help_port_.reset();
  error_help_module_.reset();
  timesync_known_ = false;
  timesync_status_->setText(error);
  timesync_apply_->setEnabled(false);
}

void CorsPanel::setRtkModuleVersions(std::optional<prism::TimeSyncRtkVersions> s) {
  const auto version = [&](const prism::RtkModuleVersion& v) {
    if (!s || !s->linked || !v.valid) return uiText("Unavailable", "未提供");
    return QStringLiteral("%1.%2.%3%4").arg(v.major).arg(v.minor).arg(v.patch)
        .arg(v.diagnostic ? uiText(" (diagnostic)", "（诊断版）") : QString());
  };
  const prism::RtkModuleVersion empty;
  module_versions_->setText(uiText("RTK-module firmware: %1\nBootloader: %2",
                                  "RTK-module 固件版本：%1\n引导程序版本：%2")
      .arg(version(s ? s->application : empty)).arg(version(s ? s->bootloader : empty)));
  module_versions_->setToolTip(uiText("Read-only snapshot; refresh with Read status. No GNSS fix required.",
      "只读快照；点击“读取实际状态”刷新。不要求 GNSS 已定位。"));
}

void CorsPanel::setTimeSyncStatus(const prism::TimeSyncPortStatus& s,
                                 std::optional<prism::TimeSyncRtkStatus> r,
                                 const QString& module_error) {
  error_help_port_ = s;
  error_help_module_ = r;
  error_help_read_at_ = std::chrono::steady_clock::now();
  timesync_known_ = true;
  if (!timesync_dirty_) timesync_mode_->setCurrentIndex(timesync_mode_->findData(static_cast<uint32_t>(s.mode)));
  const QString name = timesync_mode_->itemText(timesync_mode_->findData(static_cast<uint32_t>(s.mode)));
  QStringList lines;
  lines << uiText("Saved on RK: %1", "RK 本地保存：%1").arg(s.persisted ? name : uiText("Not saved", "未保存"));
  lines << uiText("Applied mode: %1", "实际应用：%1").arg(s.applied && s.sensor_board_online && !s.error_code ? name : uiText("Unconfirmed", "未确认"));
  lines << QStringLiteral("Sensor Board: %1 | error=%2").arg(s.sensor_board_online ? uiText("online", "在线") : uiText("offline", "离线")).arg(s.error_code);
  lines << QStringLiteral("RTK-module: %1").arg(!r ? uiText("Unavailable: ", "不可用：") + module_error :
      r->linked ? uiText("Connected", "已连接") : uiText("Disconnected", "未连接"));
  if (r && r->linked) {
    const QStringList states{uiText("unknown", "未知"),uiText("starting", "启动中"),uiText("running", "运行中"),uiText("stopping", "停止中"),uiText("stopped", "已停止"),uiText("error", "错误")};
    const bool gnss_no_fix = r->device_status_fresh && r->control_status_fresh &&
        r->error_code == -11 && r->control_state == 0 && r->control_error == 0 &&
        (r->device_flags & 3u) == 3u && r->gnss_age_ms <= 2000 && r->fix == 0;
    lines << uiText("Control: %1 | error=%2 / %3", "控制状态：%1 | 错误=%2 / %3")
        .arg(!r->control_status_fresh ? uiText("Stale / unavailable", "已过期 / 未提供") :
             gnss_no_fix ? uiText("GNSS has no position fix", "GNSS 未定位成功") : states.value(r->control_state))
        .arg(r->error_code).arg(r->control_error);
  }
  timesync_status_->setText(lines.join(QStringLiteral("\n")));
  timesync_apply_->setEnabled(!timesync_locked_);
}

void CorsPanel::showRtkErrorHelp() {
  if (auto* existing = findChild<QDialog*>(QStringLiteral("rtkErrorDialog"))) {
    existing->raise(); existing->activateWindow(); return;
  }
  const auto& p = error_help_port_;
  const auto& r = error_help_module_;
  const bool live = device_open_ && p && std::chrono::steady_clock::now() - error_help_read_at_ < std::chrono::seconds(15);
  const auto code = [](int64_t value) { return std::optional<int64_t>(value); };
  QString html = "<p>" + uiText(
      "Snapshots from the last read. This popup sends no commands. Refresh status and reopen for an update; error codes and cumulative counts are different.",
      "以下为上次读取的快照。弹窗不发送命令；刷新状态后重新打开可查看更新。错误码与累计计数请分别判断。").toHtmlEscaped() + "</p>";
  html += rtkErrorSection("port", p ? code(p->error_code) : std::nullopt, live);
  html += rtkErrorSection("module", r ? code(r->error_code) : std::nullopt, live && r && r->linked);
  html += rtkErrorSection("control", r && r->control_status_fresh ? code(r->control_error) : std::nullopt, live && r && r->linked && r->control_status_fresh);
  html += rtkErrorSection("rtcm", r && r->device_status_fresh ? code(r->rtcm_errors) : std::nullopt, live && r && r->linked && r->device_status_fresh);
  auto* dialog = new QDialog(this);
  dialog->setObjectName(QStringLiteral("rtkErrorDialog"));
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setWindowTitle(uiText("RTK-module LED / error help", "RTK-module LED / 错误说明"));
  dialog->setStyleSheet(QStringLiteral("QDialog{background:#fff;color:#182b40;} QTextBrowser{background:#fff;color:#182b40;border:0;} QPushButton{background:#edf5ff;color:#245c9b;border:1px solid #bbd4ee;border-radius:7px;padding:8px 16px;} QPushButton:focus{border:2px solid #155cb3;} QTabWidget::pane{background:#fff;border:1px solid #c6d8ea;} QTabBar::tab{background:#edf5ff;color:#245c9b;padding:8px 14px;} QTabBar::tab:selected{background:#fff;color:#155cb3;}"));
  auto* layout = new QVBoxLayout(dialog);
  auto* tabs = new QTabWidget(dialog);
  for (const auto& page : {std::make_pair(uiText("LED patterns", "LED 闪烁说明"), rtkLedReference()),
                           std::make_pair(uiText("Current snapshot", "当前快照"), html),
                           std::make_pair(uiText("Error code reference", "错误码说明"), rtkErrorReference())}) {
    auto* text = new QTextBrowser(tabs);
    text->setOpenExternalLinks(false);
    text->setOpenLinks(false);
    text->setHtml(page.second);
    tabs->addTab(text, page.first);
  }
  layout->addWidget(tabs);
  auto* buttons = new QDialogButtonBox(dialog);
  buttons->addButton(uiText("Close", "关闭"), QDialogButtonBox::RejectRole);
  connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
  layout->addWidget(buttons);
  dialog->resize(680, 520);
  dialog->show();
}

void CorsPanel::populateProviderOptions() {
  const QString previous_mount = mountpoint_selector_->currentData().toString();
  // Preserve the coordinate system, not the previous provider's port number.
  const bool use_cgcs2000 = coordinate_system_selector_->currentIndex() == 1;
  endpoint_selector_->clear();
  mountpoint_selector_->clear();
  const auto* provider = cors::findCorsServiceProvider(
      provider_selector_->currentData().toString());
  if (provider == nullptr || provider->endpoints.isEmpty()) return;
  coordinate_system_selector_->clear();
  coordinate_system_selector_->addItem(QStringLiteral("WGS84 — 8002"), 8002);
  coordinate_system_selector_->addItem(
      QStringLiteral("CGCS2000 — %1").arg(provider->cgcs2000_port),
      provider->cgcs2000_port);
  coordinate_system_selector_->setCurrentIndex(use_cgcs2000 ? 1 : 0);
  const QString primary = provider->endpoints.front().host;
  if (provider->endpoints.size() > 1) {
    const QString backup = provider->endpoints.at(1).host;
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
  } else {
    endpoint_selector_->addItem(primary, QStringLiteral("primary"));
  }
  for (const auto& mountpoint : provider->mountpoints) {
    mountpoint_selector_->addItem(mountpoint.display_name, mountpoint.id);
  }
  const int previous_index = mountpoint_selector_->findData(previous_mount);
  if (previous_index >= 0) mountpoint_selector_->setCurrentIndex(previous_index);
}

void CorsPanel::loadSettings() {
  QSettings settings(QSettings::defaultFormat(), QSettings::UserScope,
                     QStringLiteral("DIBULI"), QStringLiteral("PrismViewer"));
  settings.beginGroup(QStringLiteral("cors"));
  const QString provider =
      settings.value(QStringLiteral("serviceProvider"),
                     QStringLiteral("china_mobile"))
          .toString();
  int index = provider_selector_->findData(provider);
  if (index >= 0) provider_selector_->setCurrentIndex(index);
  populateProviderOptions();
  const QString endpoint_policy =
      settings.value(QStringLiteral("endpointPolicy"),
                     QStringLiteral("automatic"))
          .toString();
  if (endpoint_policy == QStringLiteral("custom")) {
    const QString custom_address =
        settings.value(QStringLiteral("endpointAddress")).toString();
    if (!custom_address.isEmpty()) {
      endpoint_selector_->setCurrentIndex(-1);
      endpoint_selector_->setEditText(custom_address);
    }
  } else {
    index = endpoint_selector_->findData(endpoint_policy);
    if (index >= 0) endpoint_selector_->setCurrentIndex(index);
  }
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
  settings.endGroup();
}

void CorsPanel::saveSettings() {
  QSettings settings(QSettings::defaultFormat(), QSettings::UserScope,
                     QStringLiteral("DIBULI"), QStringLiteral("PrismViewer"));
  settings.beginGroup(QStringLiteral("cors"));
  settings.setValue(QStringLiteral("serviceProvider"),
                    provider_selector_->currentData());
  const int endpoint_index = endpoint_selector_->currentIndex();
  const bool preset =
      endpoint_index >= 0 &&
      endpoint_selector_->currentText() ==
          endpoint_selector_->itemText(endpoint_index);
  settings.setValue(
      QStringLiteral("endpointPolicy"),
      preset ? endpoint_selector_->currentData() : QStringLiteral("custom"));
  if (preset) {
    settings.remove(QStringLiteral("endpointAddress"));
  } else {
    settings.setValue(QStringLiteral("endpointAddress"),
                      endpoint_selector_->currentText().trimmed());
  }
  settings.setValue(QStringLiteral("port"),
                    coordinate_system_selector_->currentData());
  settings.setValue(QStringLiteral("mountpoint"),
                    mountpoint_selector_->currentData());
  settings.setValue(QStringLiteral("username"), username_edit_->text());
  settings.remove(QStringLiteral("latitude"));
  settings.remove(QStringLiteral("longitude"));
  settings.remove(QStringLiteral("altitude"));
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
  result.mountpoint = mountpoint_selector_->currentData().toString();
  QString validation;
  if (provider != nullptr) {
    const int endpoint_index = endpoint_selector_->currentIndex();
    const bool preset =
        endpoint_index >= 0 &&
        endpoint_selector_->currentText() ==
            endpoint_selector_->itemText(endpoint_index);
    const QString endpoint_policy =
        preset ? endpoint_selector_->currentData().toString() : QString();
    if (!preset) {
      const auto parsed = cors::parseCorsEndpointAddress(
          endpoint_selector_->currentText(),
          static_cast<quint16>(
              coordinate_system_selector_->currentData().toUInt()));
      if (!parsed.valid()) {
        validation = parsed.error;
      } else {
        result.endpoints.push_back(parsed.endpoint);
        if (!parsed.mountpoint.isEmpty()) {
          result.mountpoint = parsed.mountpoint;
        }
      }
    } else if (endpoint_policy == QStringLiteral("primary")) {
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
    if (preset) {
      for (auto& endpoint : result.endpoints) endpoint.port = port;
    }
  }
  result.username = username_edit_->text();
  result.password = password_edit_->text();
  const auto live_gga =
      gnss_timing_status_.has_value()
          ? cors::corsGgaDataFromGnssStatus(*gnss_timing_status_)
          : std::optional<cors::CorsGgaData>{};
  if (live_gga.has_value()) {
    result.latitude_degrees = live_gga->latitude_degrees;
    result.longitude_degrees = live_gga->longitude_degrees;
    result.altitude_meters = live_gga->altitude_meters;
  }
  if (validation.isEmpty() && !live_gga.has_value()) {
    validation = uiText(
        "A fresh valid device GNSS position is required before connecting CORS",
        "连接 CORS 前需要设备提供有效且新鲜的 GNSS 定位");
  } else if (validation.isEmpty()) {
    validation = cors::validateCorsConfiguration(result);
  }
  if (error != nullptr) *error = validation;
  return result;
}

void CorsPanel::resetLocalOrigin() {
  local_origin_valid_ = false;
  local_origin_latitude_degrees_ = 0.0;
  local_origin_longitude_degrees_ = 0.0;
  local_origin_ellipsoidal_height_meters_ = 0.0;
  local_origin_source_.clear();
}

void CorsPanel::ensureLocalOrigin(double latitude_degrees,
                                  double longitude_degrees,
                                  double ellipsoidal_height_meters,
                                  const QString& source) {
  if (local_origin_valid_ || !std::isfinite(latitude_degrees) ||
      !std::isfinite(longitude_degrees) ||
      !std::isfinite(ellipsoidal_height_meters) ||
      latitude_degrees < -90.0 || latitude_degrees > 90.0 ||
      longitude_degrees < -180.0 || longitude_degrees > 180.0) {
    return;
  }
  local_origin_valid_ = true;
  local_origin_latitude_degrees_ = latitude_degrees;
  local_origin_longitude_degrees_ = longitude_degrees;
  local_origin_ellipsoidal_height_meters_ = ellipsoidal_height_meters;
  local_origin_source_ = source;
}

QString CorsPanel::formatPosition(double latitude_degrees,
                                  double longitude_degrees,
                                  double ellipsoidal_height_meters,
                                  const QString& details) const {
  QString text;
  if (local_origin_valid_) {
    const CartesianPosition enu = geodeticToEnu(
        latitude_degrees, longitude_degrees, ellipsoidal_height_meters,
        local_origin_latitude_degrees_, local_origin_longitude_degrees_,
        local_origin_ellipsoidal_height_meters_);
    text = uiText("Local ENU (m): E=%1 | N=%2 | U=%3",
                  "本地 ENU（米）：E=%1 | N=%2 | U=%3")
               .arg(enu.x, 0, 'f', 3)
               .arg(enu.y, 0, 'f', 3)
               .arg(enu.z, 0, 'f', 3);
  } else {
    text = uiText("Local ENU (m): waiting for the first valid fix",
                  "本地 ENU（米）：等待首个有效定位");
  }
  text +=
      uiText("\nLatitude: %1°\nLongitude: %2°\nEllipsoidal height: %3 m",
             "\n纬度：%1°\n经度：%2°\n椭球高：%3 m")
          .arg(latitude_degrees, 0, 'f', 9)
          .arg(longitude_degrees, 0, 'f', 9)
          .arg(ellipsoidal_height_meters, 0, 'f', 3);
  if (!details.isEmpty()) text += QStringLiteral("\n") + details;
  return text;
}

void CorsPanel::setDeviceOpen(bool open) {
  device_open_ = open;
  if (!open) {
    error_help_port_.reset();
    error_help_module_.reset();
    gnss_timing_status_.reset();
    gnss_reception_status_.reset();
    resetLocalOrigin();
    device_configuration_ = {};
    has_device_configuration_ = false;
    configuration_busy_ = false;
    configuration_busy_message_.clear();
    configuration_error_.clear();
    const QSignalBlocker blocker(gnss_baud_combo_);
    const int default_baud_index =
        gnss_baud_combo_->findData(prism::kGnssUartDefaultBaud);
    if (default_baud_index >= 0)
      gnss_baud_combo_->setCurrentIndex(default_baud_index);
  }
  refreshView();
}

void CorsPanel::setControlsLocked(bool locked) {
  controls_locked_ = locked;
  refreshView();
}

void CorsPanel::setConfigurationBusy(bool busy, const QString& message) {
  configuration_busy_ = busy;
  configuration_busy_message_ = message;
  if (busy) configuration_error_.clear();
  refreshView();
}

void CorsPanel::setDeviceConfiguration(
    const prism::DeviceConfiguration& configuration) {
  device_configuration_ = configuration;
  has_device_configuration_ = true;
  configuration_error_.clear();
  const QSignalBlocker blocker(gnss_baud_combo_);
  const int baud_index =
      gnss_baud_combo_->findData(configuration.gnss_uart_baud);
  gnss_baud_combo_->setCurrentIndex(baud_index >= 0 ? baud_index : 0);
  refreshView();
}

void CorsPanel::setConfigurationError(const QString& error) {
  configuration_error_ = error;
  refreshView();
}

bool CorsPanel::gnssBaudDirty() const {
  return has_device_configuration_ &&
         gnss_baud_combo_->currentData().toUInt() !=
             device_configuration_.gnss_uart_baud;
}

void CorsPanel::setSessionStatus(const cors::CorsSessionStatus& status) {
  status_ = status;
  refreshView();
}


void CorsPanel::setGnssReceptionStatus(
    std::optional<prism::GnssReceptionStatus> status) {
  gnss_reception_status_ = status;
  refreshView();
}

void CorsPanel::setGnssTimingStatus(const prism::GnssTimingStatus& status) {
  if (status.nmea_position_valid) {
    ensureLocalOrigin(
        status.latitude_e7 / 10000000.0,
        status.longitude_e7 / 10000000.0,
        (status.altitude_mm + status.geoid_separation_mm) / 1000.0,
        uiText("first valid GPS fix", "首个有效 GPS 定位"));
  }
  gnss_timing_status_ = status;
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
  const bool live_gga_available =
      gnss_timing_status_.has_value() &&
      cors::corsGgaDataFromGnssStatus(*gnss_timing_status_).has_value();
  connect_button_->setEnabled(device_open_ && !active && live_gga_available);
  disconnect_button_->setEnabled(active);

  QString gnss_configuration_message;
  bool gnss_configuration_error = false;
  bool gnss_configuration_warning = false;
  if (!device_open_) {
    gnss_configuration_message =
        uiText("Open a device to read the GNSS input baud",
               "请先打开设备以读取 GNSS 输入波特率");
    gnss_configuration_warning = true;
  } else if (configuration_busy_) {
    gnss_configuration_message =
        configuration_busy_message_.isEmpty()
            ? uiText("GNSS input configuration in progress...",
                     "正在配置 GNSS 输入……")
            : configuration_busy_message_;
  } else if (!configuration_error_.isEmpty()) {
    gnss_configuration_message =
        uiText("GNSS input configuration failed: %1",
               "GNSS 输入配置失败：%1")
            .arg(configuration_error_);
    gnss_configuration_error = true;
  } else if (!has_device_configuration_) {
    gnss_configuration_message =
        uiText("GNSS input baud has not been read",
               "尚未读取 GNSS 输入波特率");
    gnss_configuration_warning = true;
  } else if (controls_locked_) {
    gnss_configuration_message =
        uiText("Wait for the current device operation to finish",
               "请等待当前设备操作完成");
    gnss_configuration_warning = true;
  } else if (active) {
    gnss_configuration_message =
        uiText("Disconnect CORS before changing the GNSS input baud",
               "修改 GNSS 输入波特率前请断开 CORS");
    gnss_configuration_warning = true;
  } else if (gnssBaudDirty()) {
    gnss_configuration_message =
        uiText("GNSS input baud has an unsaved change",
               "GNSS 输入波特率有尚未保存的修改");
    gnss_configuration_warning = true;
  } else {
    gnss_configuration_message =
        uiText("Device GNSS UART RX: %1 baud; changes apply immediately",
               "设备 GNSS UART RX：%1 baud；修改后立即生效")
            .arg(device_configuration_.gnss_uart_baud);
  }
  gnss_configuration_message_label_->setText(gnss_configuration_message);
  gnss_configuration_message_label_->setStyleSheet(
      gnss_configuration_error
          ? QStringLiteral(
                "background:#fef3f2;color:#b42318;border:1px solid #fecdca;"
                "border-radius:6px;padding:7px 9px;font-weight:600;")
          : gnss_configuration_warning
                ? QStringLiteral(
                      "background:#fffaeb;color:#b54708;border:1px solid #fedf89;"
                      "border-radius:6px;padding:7px 9px;font-weight:600;")
                : QStringLiteral(
                      "background:#ecfdf3;color:#027a48;border:1px solid #abefc6;"
                      "border-radius:6px;padding:7px 9px;font-weight:600;"));
  const bool can_configure_gnss =
      device_open_ && has_device_configuration_ && !configuration_busy_ &&
      !controls_locked_ && !active;
  gnss_baud_combo_->setEnabled(can_configure_gnss);
  gnss_refresh_button_->setEnabled(device_open_ && !configuration_busy_ &&
                                   !controls_locked_ && !active);
  gnss_apply_button_->setEnabled(can_configure_gnss && gnssBaudDirty());

  endpoint_value_->setText(
      status_.endpoint.isEmpty() ? QStringLiteral("-") : status_.endpoint);
  received_value_->setText(byteCount(status_.received_bytes));
  forwarded_value_->setText(byteCount(status_.forwarded_bytes));
  if (status_.rtk_status_valid) {
    source_value_->setText(QString::fromLatin1(
        communication::rtkBaseSourceName(status_.rtk_status.base_source)));
    solution_value_->setText(QString::fromLatin1(
        communication::rtkSolutionName(status_.rtk_status.solution)));
  } else {
    source_value_->setText(QStringLiteral("-"));
    solution_value_->setText(QStringLiteral("-"));
  }
  if (status_.rtk_status_valid) {
    agent_value_->setText(
        QStringLiteral("format=%1 | RTCM=%2 epochs=%3 fix=%4 float=%5 errors=%6")
            .arg(QString::fromLatin1(communication::rtkCorrectionFormatName(
                status_.rtk_status.correction_format)))
            .arg(status_.rtk_status.base_rtcm_messages)
            .arg(status_.rtk_status.base_observation_epochs)
            .arg(status_.rtk_status.fix_count)
            .arg(status_.rtk_status.float_count)
            .arg(status_.rtk_status.decoder_errors));
  } else {
    agent_value_->setText(QStringLiteral("-"));
  }

  local_origin_value_->setText(
      local_origin_valid_
          ? uiText("%1\nLatitude: %2° | Longitude: %3° | h=%4 m",
                   "%1\n纬度：%2° | 经度：%3° | h=%4 m")
                .arg(local_origin_source_)
                .arg(local_origin_latitude_degrees_, 0, 'f', 9)
                .arg(local_origin_longitude_degrees_, 0, 'f', 9)
                .arg(local_origin_ellipsoidal_height_meters_, 0, 'f', 3)
          : uiText("Waiting for the first valid GPS/RTK fix",
                   "等待首个有效 GPS/RTK 定位"));

  if (gnss_reception_status_ && gnss_reception_status_->reception_available) {
      const auto& reception = *gnss_reception_status_;
      const QString raw = reception.raw_data_fresh
          ? uiText("UART: receiving bytes", "UART：正在接收字节")
          : (reception.raw_data_seen
                 ? uiText("UART: no new bytes for over 2 s", "UART：超过 2 秒未收到新字节")
                 : uiText("UART: no bytes received", "UART：尚未收到字节"));
      const QString nmea = reception.nmea_sentence_fresh
          ? uiText("NMEA: valid sentences updating", "NMEA：有效报文持续更新")
          : (reception.nmea_sentence_seen
                 ? uiText("NMEA: no valid sentence for over 2 s", "NMEA：超过 2 秒没有有效报文")
                 : uiText("NMEA: no checksum-valid sentence received", "NMEA：尚未收到校验有效的报文"));
      gnss_reception_value_->setText(raw + QStringLiteral("\n") + nmea);
    } else {
      gnss_reception_value_->setText(uiText(
          "Reception details unavailable from this Agent/recording",
          "当前 Agent／录像未提供独立接收状态"));
    }
  if (gnss_timing_status_.has_value()) {
    const auto& timing = *gnss_timing_status_;
    gnss_sync_value_->setText(timing.time_synced
        ? uiText("LOCKED: external PPS and RMC accepted", "已锁定：外部 PPS 与 RMC 已通过校时验证")
        : uiText("NOT LOCKED: independent of UART/NMEA reception", "未锁定：不代表 UART／NMEA 没有收到数据"));
    if (timing.nmea_seen) {
      gnss_receiver_value_->setText(
          QStringLiteral("%1 | %2 | %3 satellites | NMEA age %4 ms | updates %5")
              .arg(timing.nmea_fix_valid
                       ? nmeaFixQualityName(timing.nmea_fix_quality)
                       : uiText("invalid", "无效"))
              .arg(nmeaFixModeName(timing.nmea_fix_mode))
              .arg(timing.satellites)
              .arg(timing.nmea_age_ms)
              .arg(timing.nmea_update_count));
    } else {
      gnss_receiver_value_->setText(
          uiText("No GGA/GSA position-quality data yet", "尚无 GGA/GSA 定位质量数据"));
    }
    gnss_dop_value_->setText(
        timing.nmea_dop_valid
            ? QStringLiteral("PDOP=%1 | HDOP=%2 | VDOP=%3")
                  .arg(timing.pdop_milli / 1000.0, 0, 'f', 2)
                  .arg(timing.hdop_milli / 1000.0, 0, 'f', 2)
                  .arg(timing.vdop_milli / 1000.0, 0, 'f', 2)
            : QStringLiteral("-"));
    if (timing.nmea_position_valid) {
      const double msl_altitude_m = timing.altitude_mm / 1000.0;
      const double geoid_separation_m =
          timing.geoid_separation_mm / 1000.0;
      gnss_position_value_->setText(
          formatPosition(
              timing.latitude_e7 / 10000000.0,
              timing.longitude_e7 / 10000000.0,
              msl_altitude_m + geoid_separation_m,
              uiText("MSL altitude: %1 m | geoid separation: %2 m",
                     "海拔高：%1 m | 大地水准面差：%2 m")
                  .arg(msl_altitude_m, 0, 'f', 3)
                  .arg(geoid_separation_m, 0, 'f', 3)));
      gnss_utc_value_->setText(
          QTime(0, 0)
              .addMSecs(static_cast<int>(timing.utc_ms_of_day))
              .toString(QStringLiteral("HH:mm:ss.zzz 'UTC'")));
    } else {
      gnss_position_value_->setText(
          timing.nmea_seen
              ? uiText("No valid GGA position", "暂无有效 GGA 位置")
              : QStringLiteral("-"));
      gnss_utc_value_->setText(QStringLiteral("-"));
    }
    QStringList time_parts;
    if (timing.pps_detected) {
      time_parts.push_back(
          uiText("PPS high %1 ms (%2)", "PPS 高电平 %1 ms（%2）")
              .arg(timing.pps_high_width_us / 1000.0, 0, 'f', 3)
              .arg(timing.pps_valid ? uiText("valid", "有效")
                                    : uiText("invalid", "无效")));
    } else {
      time_parts.push_back(
          uiText("PPS not detected", "未检测到 PPS"));
    }
    if (timing.offset_fresh) {
      time_parts.push_back(
          uiText("NMEA-PPS delay %1 ms", "NMEA-PPS 延迟 %1 ms")
              .arg(timing.message_pps_offset_us / 1000.0, 0, 'f', 3));
    }
    if (timing.last_pps_epoch_us != 0U) {
      time_parts.push_back(
          uiText("PPS label %1", "PPS 时间标签 %1")
              .arg(QDateTime::fromMSecsSinceEpoch(
                       static_cast<qint64>(timing.last_pps_epoch_us / 1000U))
                       .toUTC()
                       .toString(
                           QStringLiteral("yyyy-MM-dd HH:mm:ss 'UTC'"))));
    }
    gnss_timing_value_->setText(time_parts.join(QStringLiteral(" | ")));
  } else {
    gnss_receiver_value_->setText(QStringLiteral("-"));
    gnss_sync_value_->setText(QStringLiteral("-"));
    gnss_dop_value_->setText(QStringLiteral("-"));
    gnss_position_value_->setText(QStringLiteral("-"));
    gnss_utc_value_->setText(QStringLiteral("-"));
    gnss_timing_value_->setText(QStringLiteral("-"));
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
        "Receiving RTCM 2.x/3.x and forwarding it to the RK Agent",
        "正在接收 RTCM 2.x/3.x 并转发给 RK Agent");
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
