#pragma once

#include "cors/cors_config.hpp"
#include "cors/cors_session.hpp"
#include "communication/rtk_corrections.hpp"
#include "prism/usb/configuration.hpp"

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
  void setSessionStatus(const cors::CorsSessionStatus& status);
  void setNavigationStatus(
      const communication::RtkNavigationStatus& status,
      bool from_dataset = false);
  void setGnssTimingStatus(const prism::GnssTimingStatus& status);
  void setDeviceTimeUs(uint64_t device_time_us);
  void setNavigationUnavailable(const QString& reason = {});
  cors::CorsConfiguration configuration(QString* error = nullptr) const;

  std::function<void(const cors::CorsConfiguration&)> on_connect;
  std::function<void()> on_disconnect;
  std::function<void()> on_gnss_refresh;
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
  QLabel* update_rate_value_ = nullptr;
  QLabel* epoch_value_ = nullptr;
  QLabel* position_value_ = nullptr;
  QLabel* raw_position_value_ = nullptr;
  QLabel* precision_value_ = nullptr;
  QLabel* confidence_value_ = nullptr;
  QLabel* differential_value_ = nullptr;
  QLabel* gnss_receiver_value_ = nullptr;
  QLabel* gnss_dop_value_ = nullptr;
  QLabel* local_origin_value_ = nullptr;
  QLabel* gnss_position_value_ = nullptr;
  QLabel* gnss_utc_value_ = nullptr;
  QLabel* gnss_timing_value_ = nullptr;
  QLabel* agent_value_ = nullptr;
  QLabel* gnss_configuration_message_label_ = nullptr;
  QComboBox* gnss_baud_combo_ = nullptr;
  QPushButton* gnss_refresh_button_ = nullptr;
  QPushButton* gnss_apply_button_ = nullptr;
  QPushButton* connect_button_ = nullptr;
  QPushButton* disconnect_button_ = nullptr;

  prism::DeviceConfiguration device_configuration_;
  cors::CorsSessionStatus status_;
  communication::RtkNavigationStatus navigation_status_;
  std::optional<prism::GnssTimingStatus> gnss_timing_status_;
  QString navigation_unavailable_reason_;
  bool navigation_status_valid_ = false;
  bool navigation_from_dataset_ = false;
  int64_t previous_navigation_epoch_us_ = 0;
  uint64_t previous_navigation_solution_count_ = 0;
  double navigation_rate_hz_ = 0.0;
  uint64_t device_time_anchor_us_ = 0;
  std::chrono::steady_clock::time_point device_time_anchor_received_at_{};
  bool device_time_valid_ = false;
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
