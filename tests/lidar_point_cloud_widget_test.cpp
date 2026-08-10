#include "ui/lidar_point_cloud_widget.hpp"

#include <QtWidgets/QApplication>

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
  widget.clearPoints();
  require(widget.pointCount() == 0u, "point clear remains available");
  require(widget.pointSize() ==
              prism_viewer::LidarPointCloudWidget::kMaximumPointSize,
          "clearing points does not reset the selected point size");

  std::cout << "lidar point cloud widget tests passed\n";
  return 0;
}
