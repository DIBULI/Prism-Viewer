#include "ui/cors_panel.hpp"

#include "common/ui_text.hpp"

#include <QtCore/QSettings>
#include <QtCore/QDateTime>
#include <QtCore/QSignalBlocker>
#include <QtCore/QTime>
#include <QtWidgets/QCheckBox>
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

QString smoothingStateName(uint32_t flags) {
  using namespace prism_viewer::communication;
  if ((flags & RtkSmoothingJumpGated) != 0u) {
    return uiText("jump gated", "跳变门控");
  }
  if ((flags & RtkSmoothingTransitionGated) != 0u) {
    return uiText("quality transition gated", "解状态切换门控");
  }
  if ((flags & RtkSmoothingResetEpochGap) != 0u) {
    return uiText("reset after epoch gap", "历元断档后重置");
  }
  if ((flags & RtkSmoothingResetBaseSource) != 0u) {
    return uiText("reset after base change", "基站源切换后重置");
  }
  return (flags & RtkSmoothingDynamicsEnabled) != 0u
             ? uiText("active", "运行中")
             : uiText("disabled", "未启用");
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
  update_rate_value_ = new QLabel(QStringLiteral("-"), status_group);
  update_rate_value_->setObjectName(QStringLiteral("rtkUpdateRate"));
  epoch_value_ = new QLabel(QStringLiteral("-"), this);
  epoch_value_->setObjectName(QStringLiteral("rtkSolutionEpoch"));
  position_value_ = new QLabel(QStringLiteral("-"), this);
  position_value_->setObjectName(QStringLiteral("rtkPosition"));
  raw_position_value_ = new QLabel(QStringLiteral("-"), this);
  raw_position_value_->setObjectName(QStringLiteral("rtkRawPosition"));
  precision_value_ = new QLabel(QStringLiteral("-"), this);
  precision_value_->setObjectName(QStringLiteral("rtkPrecision"));
  confidence_value_ = new QLabel(QStringLiteral("-"), this);
  confidence_value_->setObjectName(QStringLiteral("rtkConfidence"));
  differential_value_ = new QLabel(QStringLiteral("-"), status_group);
  differential_value_->setObjectName(QStringLiteral("rtkDifferential"));
  gnss_receiver_value_ = new QLabel(QStringLiteral("-"), this);
  gnss_receiver_value_->setObjectName(QStringLiteral("gnssReceiverQuality"));
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
  update_rate_value_->setWordWrap(true);
  differential_value_->setWordWrap(true);
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
                 uiText("Navigation rate", "导航更新率"), update_rate_value_);
  addStatusField(status_layout, status_group,
                 uiText("Differential", "差分状态"), differential_value_);
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
  const auto rtk_tab = make_scroll_tab(
      QStringLiteral("rtkStatusTab"), QStringLiteral("rtkStatusScroll"),
      QStringLiteral("RTK"));
  QWidget* const gps_content = gps_tab.first;
  QVBoxLayout* const gps_layout = gps_tab.second;
  QWidget* const rtk_content = rtk_tab.first;
  QVBoxLayout* const rtk_layout = rtk_tab.second;
  auto* gnss_heading = new QLabel(
      uiText("Live receiver and position at 10 Hz; basic PPS status at 1 Hz",
             "接收机与位置按 10 Hz 刷新；基础 PPS 状态按 1 Hz 更新"),
      gps_content);
  gnss_heading->setWordWrap(true);
  gnss_heading->setStyleSheet(
      QStringLiteral("color:#475467;padding:2px 0 6px 0;"));
  gps_layout->addWidget(gnss_heading);

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

  epoch_value_->setParent(gnss_group);
  position_value_->setParent(gnss_group);
  raw_position_value_->setParent(gnss_group);
  precision_value_->setParent(gnss_group);
  confidence_value_->setParent(gnss_group);
  gnss_receiver_value_->setParent(gnss_group);
  gnss_dop_value_->setParent(gnss_group);
  local_origin_value_->setParent(gnss_group);
  gnss_position_value_->setParent(gnss_group);
  gnss_utc_value_->setParent(gnss_group);
  gnss_timing_value_->setParent(gnss_group);
  addStatusField(gps_layout, gps_content,
                 uiText("GPS fix", "GPS 定位"), gnss_receiver_value_);
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

  addStatusField(rtk_layout, rtk_content,
                 uiText("RTK position (smoothed)", "RTK 位置（平滑）"),
                 position_value_);
  addStatusField(rtk_layout, rtk_content,
                 uiText("RTK position (raw)", "RTK 位置（原始）"),
                 raw_position_value_);
  addStatusField(rtk_layout, rtk_content,
                 uiText("RTK solution epoch", "RTK 解算历元"), epoch_value_);
  addStatusField(rtk_layout, rtk_content,
                 uiText("Estimated precision", "估计精度"), precision_value_);
  addStatusField(rtk_layout, rtk_content,
                 uiText("Credibility", "可信度"), confidence_value_);
  rtk_layout->addStretch(1);
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
  QSettings settings(QStringLiteral("DIBULI"), QStringLiteral("PrismViewer"));
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
    device_time_anchor_us_ = 0;
    device_time_valid_ = false;
    gnss_timing_status_.reset();
    resetLocalOrigin();
    setNavigationUnavailable();
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

void CorsPanel::setNavigationStatus(
    const communication::RtkNavigationStatus& status,
    bool from_dataset) {
  if (navigation_status_valid_ && navigation_from_dataset_ != from_dataset) {
    resetLocalOrigin();
  }
  if (status.smoothed_position_valid) {
    ensureLocalOrigin(
        status.smoothed_latitude_deg, status.smoothed_longitude_deg,
        status.smoothed_ellipsoidal_height_m,
        from_dataset ? uiText("first dataset RTK fix", "数据集首个 RTK 定位")
                     : uiText("first live RTK fix", "实时首个 RTK 定位"));
  } else if (status.solution_valid) {
    ensureLocalOrigin(
        status.latitude_deg, status.longitude_deg,
        status.ellipsoidal_height_m,
        from_dataset ? uiText("first dataset RTK fix", "数据集首个 RTK 定位")
                     : uiText("first live RTK fix", "实时首个 RTK 定位"));
  }
  if (status.solution_epoch_us > previous_navigation_epoch_us_ &&
      status.solution_count > previous_navigation_solution_count_) {
    const int64_t elapsed_us =
        status.solution_epoch_us - previous_navigation_epoch_us_;
    const uint64_t elapsed_solutions =
        status.solution_count - previous_navigation_solution_count_;
    if (previous_navigation_epoch_us_ > 0 && elapsed_us > 0) {
      navigation_rate_hz_ =
          static_cast<double>(elapsed_solutions) * 1000000.0 /
          static_cast<double>(elapsed_us);
    }
  }
  previous_navigation_epoch_us_ = status.solution_epoch_us;
  previous_navigation_solution_count_ = status.solution_count;
  navigation_status_ = status;
  navigation_status_valid_ = true;
  navigation_from_dataset_ = from_dataset;
  navigation_unavailable_reason_.clear();
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

void CorsPanel::setDeviceTimeUs(uint64_t device_time_us) {
  device_time_anchor_us_ = device_time_us;
  device_time_anchor_received_at_ = std::chrono::steady_clock::now();
  device_time_valid_ = device_time_us != 0u;
  refreshView();
}

void CorsPanel::setNavigationUnavailable(const QString& reason) {
  navigation_status_valid_ = false;
  navigation_from_dataset_ = false;
  navigation_unavailable_reason_ = reason;
  previous_navigation_epoch_us_ = 0;
  previous_navigation_solution_count_ = 0;
  navigation_rate_hz_ = 0.0;
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
  if (navigation_status_valid_) {
    source_value_->setText(QString::fromLatin1(
        communication::rtkBaseSourceName(navigation_status_.base_source)));
    const QString raw_solution = QString::fromLatin1(
        communication::rtkSolutionName(navigation_status_.solution));
    solution_value_->setText(
        navigation_status_.smoothed_position_valid
            ? QStringLiteral("smoothed=%1 | raw=%2")
                  .arg(QString::fromLatin1(communication::rtkSolutionName(
                           navigation_status_.smoothed_solution)),
                       raw_solution)
            : uiText("raw=%1 | smoothed unavailable",
                     "原始=%1 | 平滑解不可用")
                  .arg(raw_solution));
  } else if (status_.rtk_status_valid) {
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

  if (navigation_status_valid_) {
    update_rate_value_->setText(
        navigation_rate_hz_ > 0.0
            ? QStringLiteral("%1 Hz (solution epoch driven)")
                  .arg(navigation_rate_hz_, 0, 'f', 2)
            : uiText("waiting for the next epoch", "等待下一解算历元"));
    const auto format_epoch = [this](int64_t epoch_us, bool valid) {
      if (!valid || epoch_us <= 0) return QStringLiteral("-");
      QString epoch_text =
          QDateTime::fromMSecsSinceEpoch(epoch_us / 1000)
              .toUTC()
              .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz 'UTC'"));
      if (!navigation_from_dataset_ && device_time_valid_) {
        const double elapsed_us =
            std::chrono::duration<double, std::micro>(
                std::chrono::steady_clock::now() -
                device_time_anchor_received_at_)
                .count();
        const double age_seconds =
            (static_cast<double>(device_time_anchor_us_) + elapsed_us -
             static_cast<double>(epoch_us)) /
            1000000.0;
        if (age_seconds >= -0.001) {
          epoch_text = uiText("%1 | %2 s ago (device clock)",
                              "%1 | 设备时间 %2 秒前")
                           .arg(epoch_text)
                           .arg(std::max(0.0, age_seconds), 0, 'f', 3);
        } else {
          epoch_text = uiText(
                           "%1 | %2 s ahead of device clock",
                           "%1 | 比设备时间快 %2 秒")
                           .arg(epoch_text)
                           .arg(-age_seconds, 0, 'f', 3);
        }
      }
      return epoch_text;
    };
    const QString raw_epoch = format_epoch(
        navigation_status_.solution_epoch_us,
        navigation_status_.solution_valid);
    epoch_value_->setText(
        navigation_status_.smoothed_position_valid
            ? uiText("smoothed: %1\nraw: %2", "平滑：%1\n原始：%2")
                  .arg(format_epoch(
                           navigation_status_.smoothed_solution_epoch_us,
                           true),
                       raw_epoch)
            : uiText("raw: %1\nsmoothed: unavailable",
                     "原始：%1\n平滑：不可用")
                  .arg(raw_epoch));
    position_value_->setText(
        navigation_status_.smoothed_position_valid
            ? formatPosition(
                  navigation_status_.smoothed_latitude_deg,
                  navigation_status_.smoothed_longitude_deg,
                  navigation_status_.smoothed_ellipsoidal_height_m,
                  uiText("Satellites: %1", "卫星数：%1")
                      .arg(navigation_status_.satellites))
            : uiText("Waiting for a quality-gated smoothed solution",
                     "等待通过质量门控的平滑解"));
    raw_position_value_->setText(
        navigation_status_.solution_valid
            ? formatPosition(
                  navigation_status_.latitude_deg,
                  navigation_status_.longitude_deg,
                  navigation_status_.ellipsoidal_height_m,
                  uiText("Satellites: %1", "卫星数：%1")
                      .arg(navigation_status_.satellites))
            : uiText("No valid raw RTK solution", "尚无有效原始 RTK 解"));
    precision_value_->setText(
        navigation_status_.smoothed_position_valid
            ? QStringLiteral("smoothed E=%1 m N=%2 m U=%3 m | "
                             "raw E=%4 m N=%5 m U=%6 m")
                  .arg(navigation_status_.smoothed_east_std_m, 0, 'f', 3)
                  .arg(navigation_status_.smoothed_north_std_m, 0, 'f', 3)
                  .arg(navigation_status_.smoothed_up_std_m, 0, 'f', 3)
                  .arg(navigation_status_.east_std_m, 0, 'f', 3)
                  .arg(navigation_status_.north_std_m, 0, 'f', 3)
                  .arg(navigation_status_.up_std_m, 0, 'f', 3)
            : navigation_status_.solution_valid
                  ? QStringLiteral("raw E=%1 m N=%2 m U=%3 m")
                        .arg(navigation_status_.east_std_m, 0, 'f', 3)
                        .arg(navigation_status_.north_std_m, 0, 'f', 3)
                        .arg(navigation_status_.up_std_m, 0, 'f', 3)
                  : QStringLiteral("-"));
    confidence_value_->setText(
        navigation_status_.confidence_valid
            ? QStringLiteral("%1 (%2/1000, reasons=0x%3)")
                  .arg(QString::fromLatin1(communication::rtkConfidenceName(
                           navigation_status_.confidence)))
                  .arg(navigation_status_.confidence_score)
                  .arg(navigation_status_.confidence_reasons, 0, 16)
            : uiText("unavailable", "不可用"));
    differential_value_->setText(
        QStringLiteral("age=%1 s ratio=%2 station=%3 jump=%4")
            .arg(navigation_status_.differential_age_s, 0, 'f', 2)
            .arg(navigation_status_.ambiguity_ratio, 0, 'f', 2)
            .arg(navigation_status_.base_station_id)
            .arg(navigation_status_.position_jump_valid
                     ? QStringLiteral("%1 m")
                           .arg(navigation_status_.position_jump_m, 0, 'f', 3)
                     : QStringLiteral("-")));
    agent_value_->setText(
        QStringLiteral("rover epochs=%1 base epochs=%2 solutions=%3 "
                       "fix=%4 float=%5 errors=%6 | smoothing=%7 "
                       "resets=%8 gated=%9")
            .arg(navigation_status_.rover_observation_epochs)
            .arg(navigation_status_.base_observation_epochs)
            .arg(navigation_status_.solution_count)
            .arg(navigation_status_.fix_count)
            .arg(navigation_status_.float_count)
            .arg(navigation_status_.decoder_errors)
            .arg(smoothingStateName(navigation_status_.smoothing_flags))
            .arg(navigation_status_.smoothing_reset_count)
            .arg(navigation_status_.smoothing_gated_epoch_count));
  } else {
    update_rate_value_->setText(QStringLiteral("-"));
    epoch_value_->setText(QStringLiteral("-"));
    position_value_->setText(
        navigation_unavailable_reason_.isEmpty()
            ? QStringLiteral("-")
            : navigation_unavailable_reason_);
    raw_position_value_->setText(QStringLiteral("-"));
    precision_value_->setText(QStringLiteral("-"));
    confidence_value_->setText(QStringLiteral("-"));
    differential_value_->setText(QStringLiteral("-"));
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

  if (gnss_timing_status_.has_value()) {
    const auto& timing = *gnss_timing_status_;
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
          uiText("waiting for NMEA", "等待 NMEA 报文"));
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
    gnss_dop_value_->setText(QStringLiteral("-"));
    gnss_position_value_->setText(QStringLiteral("-"));
    gnss_utc_value_->setText(QStringLiteral("-"));
    gnss_timing_value_->setText(QStringLiteral("-"));
  }

  QString message;
  bool error = false;
  bool warning = false;
  if (navigation_from_dataset_) {
    message = uiText(
        "Playing GPS/RTK from the loaded dataset at recorded epochs",
        "正在按录制历元回放数据集 GPS/RTK");
  } else if (!device_open_) {
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
