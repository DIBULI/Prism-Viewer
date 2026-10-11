#pragma once

#include <QtCore/QList>
#include <QtCore/QPointF>
#include <QtWidgets/QWidget>

#include <array>
#include <cstddef>
#include <functional>

namespace prism_viewer::ui {

// Widget-only XYZ plot. Samples are supplied by the existing bounded IMU
// history/decimation path; rendering never changes or synthesizes samples.
class ImuTraceView final : public QWidget {
  Q_OBJECT

 public:
  struct Range {
    double minimum;
    double maximum;
    double span() const { return maximum - minimum; }
  };

  explicit ImuTraceView(QWidget* parent = nullptr);
  void setSeries(size_t axis, const QList<QPointF>& points);
  const QList<QPointF>& series(size_t axis) const;
  void setXRange(double minimum, double maximum);
  void setYRange(double minimum, double maximum);
  Range xRange() const { return x_range_; }
  Range yRange() const { return y_range_; }
  void setXTitle(const QString& title);
  void setYTitle(const QString& title);
  void setYDecimals(int decimals);
  QRectF plotRect() const;
  void resetZoom();

  std::function<void()> on_zoom_changed;
  std::function<void()> on_zoom_reset;

 protected:
  void paintEvent(QPaintEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;

 private:
  void zoom(double factor);
  bool applyZoom(Range x, Range y);
  QPointF boundedPosition(const QPointF& position) const;
  QString yLabel(double value) const;

  std::array<QList<QPointF>, 3> series_;
  Range x_range_{-10.0, 0.0};
  Range y_range_{-1.0, 1.0};
  Range reset_x_range_ = x_range_;
  Range reset_y_range_ = y_range_;
  QString x_title_;
  QString y_title_;
  int y_decimals_ = 2;
  bool dragging_ = false;
  QPointF drag_start_;
  QPointF drag_end_;
};

}  // namespace prism_viewer::ui
