#pragma once

#include "prism/usb/telemetry.hpp"

#include <QtCore/QPoint>
#include <QtWidgets/QWidget>

#include <deque>
#include <vector>

namespace prism_viewer {

class LidarPointCloudWidget : public QWidget {
 public:
  static constexpr int kMinimumPointSize = 1;
  static constexpr int kMaximumPointSize = 10;
  static constexpr int kDefaultPointSize = 2;

  explicit LidarPointCloudWidget(QWidget* parent = nullptr);

  void appendPoints(const std::vector<prism::LidarPoint>& points);
  void clearPoints();
  size_t pointCount() const noexcept;
  void setPointSize(int size);
  int pointSize() const noexcept;

 protected:
  void paintEvent(QPaintEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;

 private:
  std::deque<prism::LidarPoint> points_;
  QPoint last_mouse_position_;
  double yaw_ = -0.65;
  double pitch_ = 0.42;
  double pixels_per_meter_ = 34.0;
  int point_size_ = kDefaultPointSize;
};

}  // namespace prism_viewer
