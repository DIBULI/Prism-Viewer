#pragma once

#include "cors/cors_config.hpp"
#include "cors/cors_session.hpp"
#include "communication/rtk_corrections.hpp"

#include <QtWidgets/QWidget>

#include <functional>

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
  void setSessionStatus(const cors::CorsSessionStatus& status);
  void setNavigationStatus(
      const communication::RtkNavigationStatus& status,
      bool from_dataset = false);
  void setNavigationUnavailable(const QString& reason = {});
  cors::CorsConfiguration configuration(QString* error = nullptr) const;

  std::function<void(const cors::CorsConfiguration&)> on_connect;
  std::function<void()> on_disconnect;

 private:
  void populateProviders();
  void populateProviderOptions();
  void loadSettings();
  void saveSettings();
  void refreshView();

  QComboBox* provider_selector_ = nullptr;
  QComboBox* endpoint_selector_ = nullptr;
  QComboBox* coordinate_system_selector_ = nullptr;
  QComboBox* mountpoint_selector_ = nullptr;
  QLineEdit* username_edit_ = nullptr;
  QLineEdit* password_edit_ = nullptr;
  QCheckBox* remember_password_checkbox_ = nullptr;
  QLineEdit* latitude_edit_ = nullptr;
  QLineEdit* longitude_edit_ = nullptr;
  QLineEdit* altitude_edit_ = nullptr;
  QLabel* message_label_ = nullptr;
  QLabel* endpoint_value_ = nullptr;
  QLabel* received_value_ = nullptr;
  QLabel* forwarded_value_ = nullptr;
  QLabel* source_value_ = nullptr;
  QLabel* solution_value_ = nullptr;
  QLabel* update_rate_value_ = nullptr;
  QLabel* epoch_value_ = nullptr;
  QLabel* position_value_ = nullptr;
  QLabel* precision_value_ = nullptr;
  QLabel* confidence_value_ = nullptr;
  QLabel* differential_value_ = nullptr;
  QLabel* agent_value_ = nullptr;
  QPushButton* connect_button_ = nullptr;
  QPushButton* disconnect_button_ = nullptr;

  cors::CorsSessionStatus status_;
  communication::RtkNavigationStatus navigation_status_;
  QString navigation_unavailable_reason_;
  bool navigation_status_valid_ = false;
  bool navigation_from_dataset_ = false;
  int64_t previous_navigation_epoch_us_ = 0;
  uint64_t previous_navigation_solution_count_ = 0;
  double navigation_rate_hz_ = 0.0;
  bool device_open_ = false;
};

}  // namespace prism_viewer::ui
