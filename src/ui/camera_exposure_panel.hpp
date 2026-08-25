#pragma once

#include "prism/usb/exposure.hpp"

#include <QtCore/QString>
#include <QtWidgets/QWidget>

#include <array>
#include <functional>

class QLabel;
class QComboBox;
class QDoubleSpinBox;
class QPushButton;
class QSpinBox;

namespace prism_viewer::ui {

// Presents runtime-only camera exposure controls without owning the SDK
// client. MainWindow serializes USB commands and returns the complete
// read-back configuration and automatic limits through setConfiguration().
class CameraExposurePanel final : public QWidget {
 public:
  explicit CameraExposurePanel(QWidget* parent = nullptr);

  void clear();
  void setDeviceOpen(bool open);
  void setControlsLocked(bool locked);
  void setCaptureActive(bool active);
  void setLimitsSupported(bool supported);
  void setCameraFps(uint32_t camera_fps);
  void setBusy(bool busy, const QString& message = {});
  void setConfiguration(const prism::ExposureConfiguration& configuration,
                        const prism::ExposureLimits& limits);
  void setError(const QString& error);

  std::function<void()> on_refresh;
  std::function<void(const prism::ExposureConfiguration&,
                     const prism::ExposureLimits&)> on_apply;

 private:
  prism::ExposureConfiguration editedConfiguration() const;
  prism::ExposureLimits editedLimits() const;
  void updateLimitRanges();
  bool isDirty() const;
  void refreshView();
  void setMessage(const QString& text, bool warning, bool error);

  QLabel* message_label_ = nullptr;
  QSpinBox* target_brightness_ = nullptr;
  QSpinBox* min_exposure_us_ = nullptr;
  QSpinBox* max_exposure_us_ = nullptr;
  QDoubleSpinBox* min_gain_ = nullptr;
  QDoubleSpinBox* max_gain_ = nullptr;
  QLabel* effective_max_exposure_label_ = nullptr;
  std::array<QComboBox*, 4> camera_mode_{};
  std::array<QSpinBox*, 4> manual_exposure_us_{};
  std::array<QDoubleSpinBox*, 4> sensor_gain_{};
  QPushButton* refresh_button_ = nullptr;
  QPushButton* apply_button_ = nullptr;

  prism::ExposureConfiguration configuration_;
  prism::ExposureLimits limits_;
  uint32_t camera_fps_ = 30u;
  bool device_open_ = false;
  bool controls_locked_ = false;
  bool capture_active_ = false;
  bool limits_supported_ = true;
  bool busy_ = false;
  bool has_configuration_ = false;
  QString busy_message_;
  QString operation_error_;
};

}  // namespace prism_viewer::ui
