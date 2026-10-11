#include "ui/imu_trace_view.hpp"

#include <QtGui/QImage>
#include <QtGui/QMouseEvent>
#include <QtGui/QWheelEvent>
#include <QtWidgets/QApplication>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

using prism_viewer::ui::ImuTraceView;

namespace {
void require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}
bool near(double a, double b) { return std::abs(a - b) < 1e-8; }
void mouse(ImuTraceView& view, QEvent::Type type, QPointF position,
           Qt::MouseButton button = Qt::LeftButton) {
  QMouseEvent event(type, position, view.mapToGlobal(position.toPoint()),
                    button, type == QEvent::MouseButtonRelease
                                ? Qt::NoButton : button, Qt::NoModifier);
  QApplication::sendEvent(&view, &event);
}
void wheel(ImuTraceView& view, int delta) {
  const QPointF position = view.plotRect().center();
  QWheelEvent event(position, view.mapToGlobal(position.toPoint()), QPoint(),
                    QPoint(0, delta), Qt::NoButton, Qt::NoModifier,
                    Qt::NoScrollPhase, false);
  QApplication::sendEvent(&view, &event);
}
}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  QWidget window;
  ImuTraceView view(&window);
  view.setObjectName("trace");
  require(window.findChild<ImuTraceView*>("trace") == &view,
          "typed widget lookup works in application layout tests");
  view.resize(720, 360);
  view.setXTitle("Time to now");
  view.setYTitle("Acceleration (g)");
  view.setXRange(-10, 0);
  view.setYRange(-2, 2);
  view.setYDecimals(4);
  QPalette palette = view.palette();
  palette.setColor(QPalette::Base, Qt::white);
  palette.setColor(QPalette::Text, QColor("#20374a"));
  view.setPalette(palette);
  view.show();
  window.show();
  QApplication::processEvents();
  require(!view.grab().isNull(), "empty plot renders");
  for (size_t axis = 0; axis < 3; ++axis) {
    QList<QPointF> points;
    for (int i = 0; i <= 1000; ++i)
      points.append(QPointF(-10 + i * 0.01,
                           std::sin(i * 0.01 + axis * 2.0)));
    view.setSeries(axis, points);
  }
  require(view.series(0).size() == 1001 && view.series(3).isEmpty(),
          "XYZ samples retained without resampling");
  view.setSeries(3, {QPointF(0, 0)});
  require(view.series(2).size() == 1001, "invalid axis cannot overwrite a trace");
  const QImage screenshot = view.grab().toImage().convertToFormat(QImage::Format_RGB32);
  const std::array<QColor, 3> colors{QColor("#d92d20"), QColor("#12b76a"),
                                    QColor("#1570ef")};
  std::array<int, 3> colored_pixels{};
  for (int y = 0; y < screenshot.height(); ++y) {
    for (int x = 0; x < screenshot.width(); ++x) {
      for (size_t axis = 0; axis < colors.size(); ++axis) {
        if (screenshot.pixelColor(x, y) == colors[axis]) ++colored_pixels[axis];
      }
    }
  }
  for (int count : colored_pixels) require(count > 100, "each XYZ trace is visible");
  if (argc > 1) require(screenshot.save(QString::fromLocal8Bit(argv[1])),
                        "save rendered plot for visual inspection");

  int zooms = 0, resets = 0;
  view.on_zoom_changed = [&] { ++zooms; };
  view.on_zoom_reset = [&] { ++resets; };
  wheel(view, 120);
  require(zooms == 1 && near(view.xRange().span(), 10.0 / 1.2) &&
              near(view.yRange().span(), 4.0 / 1.2), "wheel zooms both axes");
  view.setSeries(0, {QPointF(-1, 1)});
  require(near(view.xRange().span(), 10.0 / 1.2),
          "sample refresh preserves manual zoom");
  wheel(view, -120);
  require(near(view.xRange().span(), 10.0), "wheel zoom out reverses zoom in");
  const QRectF plot = view.plotRect();
  const QPointF first = plot.topLeft() + QPointF(plot.width() * 0.25,
                                                plot.height() * 0.25);
  const QPointF last = plot.topLeft() + QPointF(plot.width() * 0.75,
                                               plot.height() * 0.75);
  mouse(view, QEvent::MouseButtonPress, first);
  mouse(view, QEvent::MouseMove, last);
  mouse(view, QEvent::MouseButtonRelease, last);
  require(view.xRange().span() > 4.8 && view.xRange().span() < 5.2 &&
              view.yRange().span() > 1.9 && view.yRange().span() < 2.1,
          "rectangle zoom maps pixel coordinates onto both data axes");
  mouse(view, QEvent::MouseButtonDblClick, view.plotRect().center());
  require(resets == 1 && near(view.xRange().minimum, -10) &&
              near(view.xRange().maximum, 0) && near(view.yRange().span(), 4),
          "double click resets axes and notifies auto-range owner");
  const int before_click = zooms;
  mouse(view, QEvent::MouseButtonPress, view.plotRect().center());
  mouse(view, QEvent::MouseButtonRelease, view.plotRect().center());
  require(zooms == before_click, "simple click does not enter manual zoom");
  mouse(view, QEvent::MouseButtonPress, QPointF(0, 0));
  mouse(view, QEvent::MouseButtonRelease, view.plotRect().center());
  require(zooms == before_click, "drag outside data area is ignored");
  mouse(view, QEvent::MouseButtonRelease, view.plotRect().center(), Qt::RightButton);
  require(near(view.xRange().span(), 20.0), "right click zooms out");
  view.resetZoom();
  const double nan = std::numeric_limits<double>::quiet_NaN();
  view.setXRange(nan, 0);
  view.setYRange(2, 2);
  require(near(view.xRange().span(), 10) && near(view.yRange().span(), 4),
          "invalid ranges leave existing valid axes untouched");
  view.setYRange(0.999, 1.001);
  wheel(view, 120);
  view.resetZoom();
  require(near(view.yRange().span(), 0.002), "constant-value padded ranges can reset");
  for (int i = 0; i < 300; ++i) wheel(view, 120);
  require(std::isfinite(view.xRange().span()) && view.xRange().span() >= 1e-12,
          "repeated zoom is bounded away from singular ranges");
  view.resetZoom();

  // Invalid points create real gaps, rather than a fabricated connecting line.
  view.setYRange(-2, 2);
  view.setSeries(0, {QPointF(-10, 1), QPointF(-7, 1), QPointF(nan, nan),
                     QPointF(-3, 1), QPointF(0, 1)});
  view.setSeries(1, {});
  view.setSeries(2, {});
  const QImage gap = view.grab().toImage();
  const QRectF data_area = view.plotRect();
  const qreal dpr = gap.devicePixelRatio();
  const int cx = qRound(data_area.center().x() * dpr);
  const int cy = qRound((data_area.bottom() - data_area.height() * 0.75) * dpr);
  for (int y = cy - 2; y <= cy + 2; ++y)
    require(gap.pixelColor(cx, y) != colors[0], "NaN gap is not connected");
  view.resize(40, 196);
  require(!view.grab().isNull(), "narrow splitter renders safely");
  std::cout << "IMU trace rendering and interaction tests passed\n";
  return 0;
}
