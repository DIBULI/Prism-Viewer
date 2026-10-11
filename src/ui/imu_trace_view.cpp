#include "ui/imu_trace_view.hpp"

#include <QtGui/QMouseEvent>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtGui/QWheelEvent>
#include <QtWidgets/QApplication>

#include <algorithm>
#include <cmath>

namespace prism_viewer::ui {
namespace {
bool validRange(ImuTraceView::Range range) {
  return std::isfinite(range.minimum) && std::isfinite(range.maximum) &&
         std::isfinite(range.span()) && range.span() >= 1e-12;
}
const std::array<QColor, 3> kColors = {
    QColor("#d92d20"), QColor("#12b76a"), QColor("#1570ef")};
}  // namespace

ImuTraceView::ImuTraceView(QWidget* parent) : QWidget(parent) {
  setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
  setMinimumSize(0, 196);
}

void ImuTraceView::setSeries(size_t axis, const QList<QPointF>& points) {
  if (axis >= series_.size()) return;
  series_[axis] = points;
  update();
}

const QList<QPointF>& ImuTraceView::series(size_t axis) const {
  static const QList<QPointF> empty;
  return axis < series_.size() ? series_[axis] : empty;
}

void ImuTraceView::setXRange(double minimum, double maximum) {
  if (!validRange({minimum, maximum})) return;
  x_range_ = reset_x_range_ = {minimum, maximum};
  update();
}

void ImuTraceView::setYRange(double minimum, double maximum) {
  if (!validRange({minimum, maximum})) return;
  y_range_ = reset_y_range_ = {minimum, maximum};
  update();
}

void ImuTraceView::setXTitle(const QString& title) {
  x_title_ = title;
  update();
}

void ImuTraceView::setYTitle(const QString& title) {
  y_title_ = title;
  update();
}

void ImuTraceView::setYDecimals(int decimals) {
  y_decimals_ = std::clamp(decimals, 0, 8);
  update();
}

QString ImuTraceView::yLabel(double value) const {
  // Scientific notation prevents large zoomed-out values crowding the plot.
  if (std::abs(value) >= 1e6) return QString::number(value, 'g', 4);
  if (std::abs(value) < 0.5 * std::pow(10.0, -y_decimals_)) value = 0.0;
  return QString::number(value, 'f', y_decimals_);
}

QRectF ImuTraceView::plotRect() const {
  const QFontMetricsF metrics(font());
  qreal label_width = 0.0;
  for (int i = 0; i < 5; ++i) {
    label_width = std::max(label_width, metrics.horizontalAdvance(
        yLabel(y_range_.minimum + y_range_.span() * i / 4.0)));
  }
  const qreal left = label_width + metrics.height() + 18.0;
  const qreal right = metrics.horizontalAdvance(QStringLiteral("-10.0 s"))
                          * 0.5 + 8.0;
  return QRectF(left, 10.0, std::max(0.0, width() - left - right),
                std::max(0.0, height() - metrics.height() * 2.0 - 30.0));
}

void ImuTraceView::paintEvent(QPaintEvent*) {
  QPainter painter(this);
  painter.fillRect(rect(), palette().base());
  const QRectF plot = plotRect();
  if (plot.width() < 2.0 || plot.height() < 2.0) return;
  const QFontMetricsF metrics(font());
  const QColor text = palette().color(QPalette::Text);
  QColor grid = text;
  grid.setAlpha(28);
  const auto map = [&](const QPointF& point) {
    // Limit off-screen coordinates before handing them to the raster engine.
    const double x = std::clamp((point.x() - x_range_.minimum) /
                                   x_range_.span(), -1e4, 1e4);
    const double y = std::clamp((point.y() - y_range_.minimum) /
                                   y_range_.span(), -1e4, 1e4);
    return QPointF(plot.left() + x * plot.width(),
                   plot.bottom() - y * plot.height());
  };
  // Reduce horizontal tick density in narrow splitters instead of overlapping.
  const qreal tick_width = std::max(
      metrics.horizontalAdvance(QString::number(x_range_.minimum, 'f', 1) + " s"),
      metrics.horizontalAdvance(QString::number(x_range_.maximum, 'f', 1) + " s"));
  const int x_ticks = std::clamp(static_cast<int>(
      plot.width() / (tick_width + 14.0)), 2, 6);
  for (int i = 0; i < x_ticks; ++i) {
    const qreal x = plot.left() + plot.width() * i / (x_ticks - 1);
    painter.setPen(grid);
    painter.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
    painter.setPen(text);
    painter.drawText(QRectF(x - tick_width * 0.5 - 4.0, plot.bottom() + 4.0,
                           tick_width + 8.0, metrics.height()), Qt::AlignCenter,
                     QString::number(x_range_.minimum +
                         x_range_.span() * i / (x_ticks - 1), 'f', 1) + " s");
  }
  for (int i = 0; i < 5; ++i) {
    const qreal y = plot.bottom() - plot.height() * i / 4.0;
    painter.setPen(grid);
    painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
    painter.setPen(text);
    painter.drawText(QRectF(metrics.height() + 4.0, y - metrics.height() * 0.5,
                           plot.left() - metrics.height() - 12.0, metrics.height()),
                     Qt::AlignRight | Qt::AlignVCenter,
                     yLabel(y_range_.minimum + y_range_.span() * i / 4.0));
  }
  painter.drawText(QRectF(plot.left(), height() - metrics.height() - 4.0,
                         plot.width(), metrics.height()), Qt::AlignCenter, x_title_);
  painter.save();
  painter.translate(metrics.height() * 0.5 + 3.0, plot.center().y());
  painter.rotate(-90.0);
  painter.drawText(QRectF(-plot.height() * 0.5, -metrics.height() * 0.5,
                         plot.height(), metrics.height()), Qt::AlignCenter,
                   metrics.elidedText(y_title_, Qt::ElideRight, plot.height()));
  painter.restore();
  painter.setPen(grid.darker(150));
  painter.drawRect(plot);

  painter.save();
  painter.setClipRect(plot);
  painter.setRenderHint(QPainter::Antialiasing, false);
  if (y_range_.minimum <= 0.0 && y_range_.maximum >= 0.0) {
    painter.setPen(QPen(QColor("#667085"), 1.0, Qt::DashLine));
    const qreal zero_y = map(QPointF(0, 0)).y();
    painter.drawLine(QPointF(plot.left(), zero_y), QPointF(plot.right(), zero_y));
  }
  for (size_t axis = 0; axis < series_.size(); ++axis) {
    QPainterPath path;
    bool connected = false;
    for (const QPointF& point : series_[axis]) {
      if (!std::isfinite(point.x()) || !std::isfinite(point.y())) {
        connected = false;  // Do not bridge invalid/missing samples.
        continue;
      }
      const QPointF pixel = map(point);
      if (connected) path.lineTo(pixel);
      else path.moveTo(pixel);
      connected = true;
    }
    QPen pen(kColors[axis], 1.5);
    pen.setCosmetic(true);
    painter.setPen(pen);
    painter.drawPath(path);
    if (series_[axis].size() == 1 && connected)
      painter.drawPoint(map(series_[axis].front()));
  }
  painter.restore();
  if (dragging_) {
    const QRectF selection = QRectF(drag_start_, drag_end_).normalized();
    QColor fill = palette().color(QPalette::Highlight);
    fill.setAlpha(40);
    painter.fillRect(selection, fill);
    painter.setPen(palette().color(QPalette::Highlight));
    painter.drawRect(selection);
  }
}

QPointF ImuTraceView::boundedPosition(const QPointF& position) const {
  const QRectF plot = plotRect();
  return QPointF(std::clamp(position.x(), plot.left(), plot.right()),
                 std::clamp(position.y(), plot.top(), plot.bottom()));
}

bool ImuTraceView::applyZoom(Range x, Range y) {
  if (!validRange(x) || !validRange(y)) return false;
  x_range_ = x;
  y_range_ = y;
  update();
  if (on_zoom_changed) on_zoom_changed();
  return true;
}

void ImuTraceView::zoom(double factor) {
  const auto scale = [factor](Range range) {
    const double middle = range.minimum + range.span() * 0.5;
    const double half_span = range.span() * 0.5 / factor;
    return Range{middle - half_span, middle + half_span};
  };
  applyZoom(scale(x_range_), scale(y_range_));
}

void ImuTraceView::resetZoom() {
  dragging_ = false;
  x_range_ = reset_x_range_;
  y_range_ = reset_y_range_;
  update();
}

void ImuTraceView::mousePressEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton && plotRect().contains(event->pos())) {
    dragging_ = true;
    drag_start_ = drag_end_ = event->pos();
    event->accept();
    return;
  }
  QWidget::mousePressEvent(event);
}

void ImuTraceView::mouseMoveEvent(QMouseEvent* event) {
  if (dragging_) {
    drag_end_ = boundedPosition(event->pos());
    update();
    event->accept();
    return;
  }
  QWidget::mouseMoveEvent(event);
}

void ImuTraceView::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton && dragging_) {
    dragging_ = false;
    const QRectF selection = QRectF(drag_start_, boundedPosition(event->pos()))
                                 .normalized();
    const QRectF plot = plotRect();
    const int threshold = QApplication::startDragDistance();
    if (selection.width() >= threshold && selection.height() >= threshold &&
        plot.width() > 0.0 && plot.height() > 0.0) {
      const auto x = [&](double pixel) {
        return x_range_.minimum + (pixel - plot.left()) / plot.width() * x_range_.span();
      };
      const auto y = [&](double pixel) {
        return y_range_.minimum + (plot.bottom() - pixel) / plot.height() * y_range_.span();
      };
      applyZoom({x(selection.left()), x(selection.right())},
                {y(selection.bottom()), y(selection.top())});
    }
    update();
    event->accept();
    return;
  }
  if (event->button() == Qt::RightButton && plotRect().contains(event->pos())) {
    zoom(0.5);
    event->accept();
    return;
  }
  QWidget::mouseReleaseEvent(event);
}

void ImuTraceView::mouseDoubleClickEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    resetZoom();
    if (on_zoom_reset) on_zoom_reset();
    event->accept();
    return;
  }
  QWidget::mouseDoubleClickEvent(event);
}

void ImuTraceView::wheelEvent(QWheelEvent* event) {
  if (event->angleDelta().y() == 0) {
    QWidget::wheelEvent(event);
    return;
  }
  zoom(event->angleDelta().y() > 0 ? 1.2 : 1.0 / 1.2);
  event->accept();
}

}  // namespace prism_viewer::ui
