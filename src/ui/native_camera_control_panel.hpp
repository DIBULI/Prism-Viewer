#pragma once
#include "communication/native_camera_control.hpp"
#include <QtWidgets/QWidget>
#include <functional>
class QCheckBox;class QSpinBox;class QDoubleSpinBox;class QPushButton;class QLabel;
namespace prism_viewer::ui {
class NativeCameraControlPanel final:public QWidget {
 public:
  explicit NativeCameraControlPanel(QWidget* parent=nullptr);
  void setAvailability(bool connected,bool active,bool locked);
  void setBusy(bool busy);
  void setIdle(const prism::CameraControlIdleStatus&);
  void setLive(const prism::CameraControlStatus&);
  void setError(const QString&);
  void clear();
  std::function<void(const NativeControlRequest&)> on_request;
 private:
  struct Camera {
    QCheckBox *ae=nullptr,*agc=nullptr;
    QSpinBox *rows=nullptr,*min_rows=nullptr,*max_rows=nullptr,*target=nullptr,*deadband=nullptr;
    QDoubleSpinBox *gain=nullptr,*min_gain=nullptr,*max_gain=nullptr;
    QLabel* observed=nullptr;
  };
  std::array<Camera,4> cameras_{};
  QLabel* status_=nullptr;
  QPushButton *refresh_=nullptr,*apply_=nullptr;
  std::optional<prism::CameraControlIdleStatus> idle_;
  std::optional<prism::CameraControlStatus> live_;
  bool connected_=false,active_=false,locked_=false,busy_=false;
  void showConfiguration(const std::array<prism::CameraRowGainConfig,4>&);
  std::array<prism::CameraRowGainConfig,4> edited()const;
  void updateEnabled();
};
}
