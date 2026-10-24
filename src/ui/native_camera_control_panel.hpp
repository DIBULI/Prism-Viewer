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
    QSpinBox *rows=nullptr;
    QDoubleSpinBox *gain=nullptr;
    QLabel* observed=nullptr;
  };
  std::array<Camera,4> cameras_{};
  QCheckBox *ae_=nullptr,*agc_=nullptr;
  QSpinBox *min_rows_=nullptr,*max_rows_=nullptr,*target_=nullptr,*deadband_=nullptr;
  QDoubleSpinBox *min_gain_=nullptr,*max_gain_=nullptr;
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
