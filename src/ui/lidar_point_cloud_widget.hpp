#pragma once

#include "prism/usb/telemetry.hpp"

#include <QtCore/QPoint>
#include <QtWidgets/QWidget>

#include <deque>
#include <vector>

namespace prism_viewer {

class LidarPointCloudWidget : public QWidget {
 public:
  struct ViewState {
    double yaw_radians;
    double pitch_radians;
    double pixels_per_meter;
  };

  static constexpr int kMinimumPointSize = 1;
  static constexpr int kMaximumPointSize = 10;
  static constexpr int kDefaultPointSize = 2;
  static constexpr double kProjectionVerticalScale = 0.34;
  static constexpr double kDefaultYawRadians = -0.65;
  static constexpr double kDefaultPitchRadians = 0.42;
  static constexpr double kDefaultPixelsPerMeter = 34.0;
  static constexpr double kTopViewYawRadians = 0.0;
  // This is the camera-above solution that makes the screen-space Z
  // coefficient zero: -scale * sin(pitch) - cos(pitch) == 0. The other
  // solution looks up at the XY plane from below.
  static constexpr double kTopViewPitchRadians = 1.898534833575452;
  static constexpr double kMinimumPitchRadians = -1.2;
  static constexpr double kMaximumPitchRadians = kTopViewPitchRadians;

  explicit LidarPointCloudWidget(QWidget* parent = nullptr);

  void appendPoints(const std::vector<prism::LidarPoint>& points);
  void clearPoints();
  size_t pointCount() const noexcept;
  void setPointSize(int size);
  int pointSize() const noexcept;
  ViewState viewState() const noexcept;
  void setTopView();
  void resetView();

 protected:
  void paintEvent(QPaintEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;

 private:
  std::deque<prism::LidarPoint> points_;
  QPoint last_mouse_position_;
  double yaw_ = kDefaultYawRadians;
  double pitch_ = kDefaultPitchRadians;
  double pixels_per_meter_ = kDefaultPixelsPerMeter;
  int point_size_ = kDefaultPointSize;
};

}  // namespace prism_viewer
