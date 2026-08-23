#include "ui/lidar_point_cloud_widget.hpp"

#include <QtGui/QMouseEvent>
#include <QtGui/QPainter>
#include <QtGui/QWheelEvent>

#include <algorithm>
#include <cmath>

namespace prism_viewer {
namespace {

constexpr size_t kMaximumStoredPoints = 120000u;
constexpr size_t kMaximumPaintedPoints = 40000u;

QColor reflectivityColor(uint8_t reflectivity) {
  const double value = static_cast<double>(reflectivity) / 255.0;
  return QColor::fromRgbF(0.10 + 0.90 * value,
                          0.72 + 0.24 * value,
                          0.95 - 0.70 * value, 0.90);
}

}  // namespace

LidarPointCloudWidget::LidarPointCloudWidget(QWidget* parent)
    : QWidget(parent) {
  setMinimumSize(560, 420);
  setMouseTracking(true);
  setCursor(Qt::OpenHandCursor);
  setToolTip(tr("Left-drag to rotate; right-drag or Shift+left-drag to pan; "
                "use the mouse wheel to zoom"));
}

void LidarPointCloudWidget::appendPoints(
    const std::vector<prism::LidarPoint>& points) {
  for (const auto& point : points) points_.push_back(point);
  while (points_.size() > kMaximumStoredPoints) points_.pop_front();
  update();
}

void LidarPointCloudWidget::clearPoints() {
  points_.clear();
  update();
}

size_t LidarPointCloudWidget::pointCount() const noexcept {
  return points_.size();
}

void LidarPointCloudWidget::setPointSize(int size) {
  const int clamped =
      std::clamp(size, kMinimumPointSize, kMaximumPointSize);
  if (point_size_ == clamped) return;
  point_size_ = clamped;
  update();
}

int LidarPointCloudWidget::pointSize() const noexcept {
  return point_size_;
}

LidarPointCloudWidget::ViewState LidarPointCloudWidget::viewState()
    const noexcept {
  return {yaw_, pitch_, pixels_per_meter_, pan_offset_.x(), pan_offset_.y()};
}

void LidarPointCloudWidget::setTopView() {
  yaw_ = kTopViewYawRadians;
  pitch_ = kTopViewPitchRadians;
  update();
}

void LidarPointCloudWidget::resetView() {
  yaw_ = kDefaultYawRadians;
  pitch_ = kDefaultPitchRadians;
  pixels_per_meter_ = kDefaultPixelsPerMeter;
  pan_offset_ = QPointF();
  update();
}

void LidarPointCloudWidget::paintEvent(QPaintEvent*) {
  QPainter painter(this);
  painter.fillRect(rect(), QColor(7, 14, 25));
  painter.setRenderHint(QPainter::Antialiasing, false);

  const QPointF center =
      QPointF(width() * 0.5, height() * 0.57) + pan_offset_;
  const double cosine_yaw = std::cos(yaw_);
  const double sine_yaw = std::sin(yaw_);
  const double cosine_pitch = std::cos(pitch_);
  const double sine_pitch = std::sin(pitch_);
  auto project = [&](double x, double y, double z) {
    const double rotated_x = cosine_yaw * x - sine_yaw * y;
    const double rotated_y = sine_yaw * x + cosine_yaw * y;
    const double pitched_y = cosine_pitch * rotated_y - sine_pitch * z;
    const double pitched_z = sine_pitch * rotated_y + cosine_pitch * z;
    return QPointF(center.x() + rotated_x * pixels_per_meter_,
                   center.y() + pitched_y * pixels_per_meter_ *
                                    kProjectionVerticalScale -
                       pitched_z * pixels_per_meter_);
  };

  painter.setPen(QPen(QColor(59, 73, 94), 1));
  for (int meter = -10; meter <= 10; ++meter) {
    painter.drawLine(project(meter, -10, 0), project(meter, 10, 0));
    painter.drawLine(project(-10, meter, 0), project(10, meter, 0));
  }
  painter.setPen(QPen(QColor(238, 81, 81), 2));
  painter.drawLine(project(0, 0, 0), project(2, 0, 0));
  painter.setPen(QPen(QColor(72, 201, 128), 2));
  painter.drawLine(project(0, 0, 0), project(0, 2, 0));
  painter.setPen(QPen(QColor(69, 139, 255), 2));
  painter.drawLine(project(0, 0, 0), project(0, 0, 2));

  const size_t step =
      std::max<size_t>(1u, points_.size() / kMaximumPaintedPoints);
  QPen point_pen(Qt::white, point_size_, Qt::SolidLine, Qt::RoundCap);
  for (size_t index = 0; index < points_.size(); index += step) {
    const auto& point = points_[index];
    const QPointF screen = project(point.x_mm / 1000.0,
                                   point.y_mm / 1000.0,
                                   point.z_mm / 1000.0);
    if (!rect().contains(screen.toPoint())) continue;
    point_pen.setColor(reflectivityColor(point.reflectivity));
    painter.setPen(point_pen);
    painter.drawPoint(screen);
  }

  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setPen(QColor(176, 190, 211));
  painter.drawText(QRect(16, 12, width() - 32, 24),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   tr("Recent points: %1").arg(points_.size()));
  if (points_.empty()) {
    painter.setPen(QColor(137, 151, 173));
    painter.drawText(rect(), Qt::AlignCenter,
                     tr("Waiting for LiDAR point cloud"));
  }
  painter.setPen(QColor(137, 151, 173));
  painter.drawText(QRect(16, height() - 36, width() - 32, 24),
                   Qt::AlignRight | Qt::AlignVCenter,
                   tr("Left-drag: rotate  |  Right-drag: pan  |  Wheel: zoom"));
}

void LidarPointCloudWidget::mousePressEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton ||
      event->button() == Qt::RightButton) {
    last_mouse_position_ = event->pos();
    panning_ = event->button() == Qt::RightButton ||
               event->modifiers().testFlag(Qt::ShiftModifier);
    setCursor(Qt::ClosedHandCursor);
    event->accept();
  }
}

void LidarPointCloudWidget::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton ||
      event->button() == Qt::RightButton) {
    panning_ = false;
    setCursor(Qt::OpenHandCursor);
    event->accept();
  }
}

void LidarPointCloudWidget::mouseMoveEvent(QMouseEvent* event) {
  if ((event->buttons() & (Qt::LeftButton | Qt::RightButton)) == 0) return;
  const QPoint delta = event->pos() - last_mouse_position_;
  last_mouse_position_ = event->pos();
  if (panning_) {
    pan_offset_ += QPointF(delta);
  } else {
    yaw_ += static_cast<double>(delta.x()) * 0.008;
    pitch_ = std::clamp(pitch_ + static_cast<double>(delta.y()) * 0.006,
                        kMinimumPitchRadians, kMaximumPitchRadians);
  }
  update();
  event->accept();
}

void LidarPointCloudWidget::wheelEvent(QWheelEvent* event) {
  const double factor = std::pow(1.0015, event->angleDelta().y());
  pixels_per_meter_ =
      std::clamp(pixels_per_meter_ * factor, 5.0, 240.0);
  update();
  event->accept();
}

}  // namespace prism_viewer
