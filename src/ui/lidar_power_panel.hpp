#pragma once
#include <prism/usb/lidar_power.hpp>
#include <QtWidgets/QWidget>
#include <functional>
class QComboBox;
class QPushButton;
class QLabel;
namespace prism_viewer::ui {
class LidarPowerPanel final : public QWidget {
 public:
  explicit LidarPowerPanel(QWidget* parent=nullptr);
  void setAvailable(bool available, bool invalidate=false);
  void setBusy(bool busy);
  void setResult(const prism::LidarPowerStatus& status);
  void setError(const QString& error);
  void clear();
  std::function<void(prism::LidarModel,int)> on_action;
 private:
  void refresh();
  QComboBox* model_;
  QPushButton *query_, *standby_, *wake_;
  QLabel* status_;
  bool available_=false, busy_=false;
};
}
