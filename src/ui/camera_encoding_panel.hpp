#pragma once

#include "prism/usb/configuration.hpp"

#include <QtCore/QString>
#include <QtWidgets/QWidget>

#include <cstdint>
#include <functional>

class QLabel;
class QPushButton;
class QSlider;
class QSpinBox;

namespace prism_viewer::ui {

// Edits the persistent MJPEG encoder setting without owning the SDK client.
// MainWindow serializes the idle-only USB transaction and publishes the
// complete read-back configuration through setConfiguration().
class CameraEncodingPanel final : public QWidget {
 public:
  explicit CameraEncodingPanel(QWidget* parent = nullptr);

  void clear();
  void setDeviceOpen(bool open);
  void setControlsLocked(bool locked);
  void setCaptureActive(bool active);
  void setBusy(bool busy, const QString& message = {});
  void setConfiguration(const prism::DeviceConfiguration& configuration);
  void setError(const QString& error);

  std::function<void()> on_refresh;
  std::function<void(uint32_t)> on_apply;

 private:
  bool isDirty() const;
  void refreshView();
  void setMessage(const QString& text, bool warning, bool error);

  QLabel* message_label_ = nullptr;
  QLabel* quality_hint_label_ = nullptr;
  QSlider* quality_slider_ = nullptr;
  QSpinBox* quality_spin_ = nullptr;
  QPushButton* refresh_button_ = nullptr;
  QPushButton* apply_button_ = nullptr;

  prism::DeviceConfiguration configuration_;
  bool device_open_ = false;
  bool controls_locked_ = false;
  bool capture_active_ = false;
  bool busy_ = false;
  bool has_configuration_ = false;
  QString busy_message_;
  QString operation_error_;
};

}  // namespace prism_viewer::ui
