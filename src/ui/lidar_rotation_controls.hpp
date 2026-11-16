#pragma once
#include "ui/lidar_point_cloud_widget.hpp"
#include <QtCore/QObject>
class QSettings;
class QPushButton;

namespace prism_viewer::ui {
// Both entry points share one local preference and one display-only transform.
class LidarRotationControls : public QObject {
 public:
  static constexpr auto kSettingsKey = "lidar/display_rotation_rpy_degrees_v1";
  LidarRotationControls(QSettings& settings, LidarPointCloudWidget& live,
                       LidarPointCloudWidget& playback, QPushButton& live_button,
                       QPushButton& playback_button, QObject* parent = nullptr);
 private:
  using Rotation = LidarPointCloudWidget::DisplayRotation;
  QSettings& settings_;
  LidarPointCloudWidget& live_;
  LidarPointCloudWidget& playback_;
  QPushButton& live_button_;
  QPushButton& playback_button_;
  Rotation rotation_;
  void updateViews();
  void edit(QWidget* parent);
};
}
