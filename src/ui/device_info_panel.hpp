#pragma once

#include "prism/usb/common.hpp"

#include <QtCore/QString>
#include <QtWidgets/QWidget>

#include <functional>

class QLabel;
class QPushButton;
class QTableWidget;

namespace prism_viewer::ui {

// Read-only presentation of the runtime DeviceInfo returned by the Host SDK.
// MainWindow owns USB access and supplies refresh operations through the
// callback so this widget never issues competing protocol commands.
class DeviceInfoPanel final : public QWidget {
 public:
  explicit DeviceInfoPanel(QWidget* parent = nullptr);

  void clear();
  void setDeviceOpen(bool open);
  void setControlsLocked(bool locked);
  void setInfo(const prism::DeviceInfo& info);
  void setVersions(const prism::DeviceVersions& versions);
  void setError(const QString& error);
  void setVersionError(const QString& error);

  std::function<void()> on_refresh;
  std::function<void()> on_refresh_versions;

 private:
  void refreshView();

  QLabel* summary_label_ = nullptr;
  QTableWidget* table_ = nullptr;
  QPushButton* refresh_button_ = nullptr;
  QPushButton* version_refresh_button_ = nullptr;

  prism::DeviceInfo info_;
  prism::DeviceVersions versions_;
  bool device_open_ = false;
  bool controls_locked_ = false;
  bool has_info_ = false;
  bool has_versions_ = false;
  QString error_;
  QString version_error_;
};

}  // namespace prism_viewer::ui
