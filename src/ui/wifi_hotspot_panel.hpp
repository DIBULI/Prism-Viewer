#pragma once

#include <QtCore/QString>
#include <QtWidgets/QWidget>

#include <functional>

class QLabel;
class QPushButton;

namespace prism_viewer::ui {

struct WifiHotspotViewState {
  bool present = false;
  bool enabled = false;
  bool running = false;
  bool ap_running = false;
  bool dhcp_running = false;
  bool persisted = false;
  int error_code = 0;
  QString interface_name;
  QString ssid;
  QString address;
  QString error;
};

// Renders Wi-Fi hotspot state without owning the SDK client or worker thread.
// MainWindow supplies the serialized operations through these callbacks.
class WifiHotspotPanel final : public QWidget {
 public:
  explicit WifiHotspotPanel(QWidget* parent = nullptr);

  void clear();
  void setDeviceOpen(bool open);
  void setControlsLocked(bool locked);
  void setBusy(bool busy, const QString& message = {});
  void setStatus(const WifiHotspotViewState& status);
  void setError(const QString& error);

  std::function<void()> on_refresh;
  std::function<void()> on_enable;
  std::function<void()> on_disable;

 private:
  void refreshView();
  void setMessage(const QString& text, bool warning, bool error);

  QLabel* detection_value_ = nullptr;
  QLabel* interface_value_ = nullptr;
  QLabel* state_value_ = nullptr;
  QLabel* ssid_value_ = nullptr;
  QLabel* security_value_ = nullptr;
  QLabel* address_value_ = nullptr;
  QLabel* dhcp_value_ = nullptr;
  QLabel* persisted_value_ = nullptr;
  QLabel* message_label_ = nullptr;
  QPushButton* refresh_button_ = nullptr;
  QPushButton* enable_button_ = nullptr;
  QPushButton* disable_button_ = nullptr;

  WifiHotspotViewState status_;
  bool device_open_ = false;
  bool controls_locked_ = false;
  bool busy_ = false;
  bool has_status_ = false;
  QString busy_message_;
  QString operation_error_;
};

}  // namespace prism_viewer::ui
