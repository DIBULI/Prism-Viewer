#pragma once

#include "cors/cors_config.hpp"
#include "cors/cors_session.hpp"

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
  QLabel* agent_value_ = nullptr;
  QPushButton* connect_button_ = nullptr;
  QPushButton* disconnect_button_ = nullptr;

  cors::CorsSessionStatus status_;
  bool device_open_ = false;
};

}  // namespace prism_viewer::ui
