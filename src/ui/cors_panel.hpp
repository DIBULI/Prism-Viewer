#pragma once

#include "cors/cors_config.hpp"
#include "cors/cors_session.hpp"
#include "communication/rtk_corrections.hpp"
#include "prism/usb/configuration.hpp"
#include "prism/usb/gnss_reception.hpp"
#include "prism/usb/timesync_port.hpp"

#include <QtWidgets/QWidget>

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace prism_viewer::ui {

class CorsPanel final : public QWidget {
 public:
  explicit CorsPanel(QWidget* parent = nullptr);

  void setDeviceOpen(bool open);
  void setControlsLocked(bool locked);
  void setConfigurationBusy(bool busy, const QString& message = {});
  void setDeviceConfiguration(
      const prism::DeviceConfiguration& configuration);
  void setConfigurationError(const QString& error);
  bool gnssBaudDirty() const;
  void setTimeSyncLocked(bool locked);
  void setTimeSyncStatus(const prism::TimeSyncPortStatus& status,
                         std::optional<prism::TimeSyncRtkStatus> module,
                         const QString& module_error = {});
  void setTimeSyncError(const QString& error);
  void setRtkModuleVersions(std::optional<prism::TimeSyncRtkVersions> versions);
  void setSessionStatus(const cors::CorsSessionStatus& status);
  void setGnssTimingStatus(const prism::GnssTimingStatus& status);
  void setGnssReceptionStatus(std::optional<prism::GnssReceptionStatus> status);
  cors::CorsConfiguration configuration(QString* error = nullptr) const;

  std::function<void(const cors::CorsConfiguration&)> on_connect;
  std::function<void()> on_disconnect;
  std::function<void()> on_gnss_refresh;
  std::function<void(std::optional<prism::TimeSyncPortMode>)> on_timesync;
  std::function<void(const prism::DeviceConfiguration&)> on_gnss_apply;

 private:
  void populateProviders();
  void populateProviderOptions();
  void loadSettings();
  void saveSettings();
  void resetLocalOrigin();
  void ensureLocalOrigin(double latitude_degrees,
                         double longitude_degrees,
                         double ellipsoidal_height_meters,
                         const QString& source);
  QString formatPosition(double latitude_degrees,
                         double longitude_degrees,
                         double ellipsoidal_height_meters,
                         const QString& details = {}) const;
  void refreshView();
  void showRtkErrorHelp();

  QComboBox* provider_selector_ = nullptr;
  QComboBox* endpoint_selector_ = nullptr;
  QComboBox* coordinate_system_selector_ = nullptr;
  QComboBox* mountpoint_selector_ = nullptr;
  QLineEdit* username_edit_ = nullptr;
  QLineEdit* password_edit_ = nullptr;
  QCheckBox* remember_password_checkbox_ = nullptr;
  QLabel* message_label_ = nullptr;
  QLabel* endpoint_value_ = nullptr;
  QLabel* received_value_ = nullptr;
  QLabel* forwarded_value_ = nullptr;
  QLabel* source_value_ = nullptr;
  QLabel* solution_value_ = nullptr;
  QLabel* gnss_receiver_value_ = nullptr;
  QLabel* gnss_reception_value_ = nullptr;
  QLabel* gnss_sync_value_ = nullptr;
  QLabel* gnss_dop_value_ = nullptr;
  QLabel* local_origin_value_ = nullptr;
  QLabel* gnss_position_value_ = nullptr;
  QLabel* gnss_utc_value_ = nullptr;
  QLabel* gnss_timing_value_ = nullptr;
  QLabel* agent_value_ = nullptr;
  QLabel* gnss_configuration_message_label_ = nullptr;
  QComboBox* gnss_baud_combo_ = nullptr;
  QComboBox* timesync_mode_ = nullptr;
  QPushButton* timesync_refresh_ = nullptr;
  QPushButton* timesync_apply_ = nullptr;
  QLabel* timesync_status_ = nullptr;
  QLabel* module_versions_ = nullptr;
  std::optional<prism::TimeSyncPortStatus> error_help_port_;
  std::optional<prism::TimeSyncRtkStatus> error_help_module_;
  std::chrono::steady_clock::time_point error_help_read_at_{};
  bool timesync_locked_ = true;
  bool timesync_known_ = false;
  bool timesync_dirty_ = false;
  QPushButton* gnss_refresh_button_ = nullptr;
  QPushButton* gnss_apply_button_ = nullptr;
  QPushButton* connect_button_ = nullptr;
  QPushButton* disconnect_button_ = nullptr;

  prism::DeviceConfiguration device_configuration_;
  cors::CorsSessionStatus status_;
  std::optional<prism::GnssTimingStatus> gnss_timing_status_;
  std::optional<prism::GnssReceptionStatus> gnss_reception_status_;
  bool device_open_ = false;
  bool controls_locked_ = false;
  bool configuration_busy_ = false;
  bool has_device_configuration_ = false;
  bool local_origin_valid_ = false;
  double local_origin_latitude_degrees_ = 0.0;
  double local_origin_longitude_degrees_ = 0.0;
  double local_origin_ellipsoidal_height_meters_ = 0.0;
  QString local_origin_source_;
  QString configuration_busy_message_;
  QString configuration_error_;
};

}  // namespace prism_viewer::ui
