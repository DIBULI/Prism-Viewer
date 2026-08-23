#include "ui/lidar_point_cloud_widget.hpp"

#include <QtGui/QMouseEvent>
#include <QtGui/QWheelEvent>
#include <QtWidgets/QApplication>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

void require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

void dragVertically(prism_viewer::LidarPointCloudWidget* widget,
                    int delta_y) {
  const QPointF start(100.0, 100.0);
  QMouseEvent press(QEvent::MouseButtonPress, start, start, Qt::LeftButton,
                    Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(widget, &press);

  const QPointF finish(start.x(), start.y() + delta_y);
  QMouseEvent move(QEvent::MouseMove, finish, finish, Qt::NoButton,
                   Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(widget, &move);

  QMouseEvent release(QEvent::MouseButtonRelease, finish, finish,
                      Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
  QApplication::sendEvent(widget, &release);
}

void pan(prism_viewer::LidarPointCloudWidget* widget, int delta_x,
         int delta_y) {
  const QPointF start(100.0, 100.0);
  QMouseEvent press(QEvent::MouseButtonPress, start, start, Qt::RightButton,
                    Qt::RightButton, Qt::NoModifier);
  QApplication::sendEvent(widget, &press);

  const QPointF finish(start.x() + delta_x, start.y() + delta_y);
  QMouseEvent move(QEvent::MouseMove, finish, finish, Qt::NoButton,
                   Qt::RightButton, Qt::NoModifier);
  QApplication::sendEvent(widget, &move);

  QMouseEvent release(QEvent::MouseButtonRelease, finish, finish,
                      Qt::RightButton, Qt::NoButton, Qt::NoModifier);
  QApplication::sendEvent(widget, &release);
}

void zoomIn(prism_viewer::LidarPointCloudWidget* widget) {
  QWheelEvent wheel(QPointF(100.0, 100.0), QPointF(100.0, 100.0), QPoint(),
                    QPoint(0, 120), Qt::NoButton, Qt::NoModifier,
                    Qt::NoScrollPhase, false);
  QApplication::sendEvent(widget, &wheel);
}

}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  prism_viewer::LidarPointCloudWidget widget;

  require(widget.pointSize() ==
              prism_viewer::LidarPointCloudWidget::kDefaultPointSize,
          "point size uses the visible default");
  widget.setPointSize(6);
  require(widget.pointSize() == 6, "point size accepts an in-range value");
  widget.setPointSize(-1);
  require(widget.pointSize() ==
              prism_viewer::LidarPointCloudWidget::kMinimumPointSize,
          "point size clamps values below the supported range");
  widget.setPointSize(100);
  require(widget.pointSize() ==
              prism_viewer::LidarPointCloudWidget::kMaximumPointSize,
          "point size clamps values above the supported range");

  prism::LidarPoint point{};
  point.x_mm = 1000;
  point.reflectivity = 128;
  widget.appendPoints(std::vector<prism::LidarPoint>{point});
  require(widget.pointCount() == 1u, "point append remains available");

  zoomIn(&widget);
  const auto zoomed_view = widget.viewState();
  require(zoomed_view.pixels_per_meter >
              prism_viewer::LidarPointCloudWidget::kDefaultPixelsPerMeter,
          "mouse wheel changes the view zoom");

  widget.setTopView();
  const auto top_view = widget.viewState();
  require(top_view.yaw_radians ==
              prism_viewer::LidarPointCloudWidget::kTopViewYawRadians,
          "top view uses an exact axis-aligned yaw");
  require(top_view.pitch_radians ==
              prism_viewer::LidarPointCloudWidget::kTopViewPitchRadians,
          "top view uses the exact pitch required by the projection");
  require(top_view.pixels_per_meter == zoomed_view.pixels_per_meter,
          "top view preserves the selected zoom");
  require(widget.pointCount() == 1u,
          "top view does not discard displayed points");
  require(widget.pointSize() ==
              prism_viewer::LidarPointCloudWidget::kMaximumPointSize,
          "top view does not reset the selected point size");

  const double top_z_screen_coefficient =
      -prism_viewer::LidarPointCloudWidget::kProjectionVerticalScale *
          std::sin(top_view.pitch_radians) -
      std::cos(top_view.pitch_radians);
  require(std::abs(top_z_screen_coefficient) < 1e-14,
          "top view projects vertically without a residual Z tilt");
  const double top_ground_y_screen_coefficient =
      prism_viewer::LidarPointCloudWidget::kProjectionVerticalScale *
          std::cos(top_view.pitch_radians) -
      std::sin(top_view.pitch_radians);
  require(top_ground_y_screen_coefficient < 0.0,
          "top view looks from above with ground-plane positive Y upward");

  dragVertically(&widget, 10000);
  require(widget.viewState().pitch_radians ==
              prism_viewer::LidarPointCloudWidget::kMaximumPitchRadians,
          "dragging cannot flip the camera beyond top-down");
  dragVertically(&widget, -10000);
  require(widget.viewState().pitch_radians ==
              prism_viewer::LidarPointCloudWidget::kMinimumPitchRadians,
          "dragging still respects the lower pitch bound");

  const auto before_pan = widget.viewState();
  pan(&widget, 80, -45);
  const auto panned_view = widget.viewState();
  require(panned_view.pan_x_pixels == before_pan.pan_x_pixels + 80.0 &&
              panned_view.pan_y_pixels == before_pan.pan_y_pixels - 45.0,
          "right-drag pans the point-cloud view");
  require(panned_view.yaw_radians == before_pan.yaw_radians &&
              panned_view.pitch_radians == before_pan.pitch_radians,
          "panning does not change point-cloud rotation");

  widget.resetView();
  const auto reset_view = widget.viewState();
  require(reset_view.yaw_radians ==
              prism_viewer::LidarPointCloudWidget::kDefaultYawRadians,
          "reset restores the default yaw");
  require(reset_view.pitch_radians ==
              prism_viewer::LidarPointCloudWidget::kDefaultPitchRadians,
          "reset restores the default pitch");
  require(reset_view.pixels_per_meter ==
              prism_viewer::LidarPointCloudWidget::kDefaultPixelsPerMeter,
          "reset restores the default zoom");
  require(reset_view.pan_x_pixels == 0.0 &&
              reset_view.pan_y_pixels == 0.0,
          "reset clears point-cloud panning");
  require(widget.pointCount() == 1u,
          "reset does not discard displayed points");
  require(widget.pointSize() ==
              prism_viewer::LidarPointCloudWidget::kMaximumPointSize,
          "reset does not change the selected point size");

  widget.clearPoints();
  require(widget.pointCount() == 0u, "point clear remains available");

  std::cout << "lidar point cloud widget tests passed\n";
  return 0;
}
