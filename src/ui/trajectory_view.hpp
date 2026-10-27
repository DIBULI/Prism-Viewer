#pragma once

#include <QPointF>
#include <QRectF>
#include <QString>
#include <algorithm>
#include <cmath>

namespace prism_viewer::ui {

// Display geometry only. ENU data, precision and recording are never modified.
struct TrajectoryView {
  static constexpr double min_scale = 1e-5;
  static constexpr double max_scale = 1e5; // 1 mm is 100 logical pixels.
  bool automatic = true;
  double pixels_per_meter = 100;
  QPointF center;

  void reset() { automatic = true; }
  void fit(const QRectF& bounds, const QRectF& area) {
    if (!automatic) return;
    center = bounds.center();
    pixels_per_meter = std::clamp(
        .85 * std::min(area.width() / std::max(bounds.width(), .01),
                       area.height() / std::max(bounds.height(), .01)),
        min_scale, max_scale);
  }
  QPointF screen(const QPointF& world, const QRectF& area) const {
    return area.center() + (world - center) * pixels_per_meter;
  }
  QPointF world(const QPointF& pixel, const QRectF& area) const {
    return center + (pixel - area.center()) / pixels_per_meter;
  }
  void zoom(double factor, const QPointF& anchor, const QRectF& area) {
    if (!std::isfinite(factor) || factor <= 0) return;
    const auto at = world(anchor, area);
    pixels_per_meter = std::clamp(pixels_per_meter * factor, min_scale, max_scale);
    center = at - (anchor - area.center()) / pixels_per_meter;
    automatic = false;
  }
  void pan(const QPointF& pixels) {
    center -= pixels / pixels_per_meter;
    automatic = false;
  }
  // Largest 1/2/5 distance no longer than the target bar, in world meters.
  double rulerMeters(double target_pixels = 100) const {
    const double wanted = std::max(1.0, target_pixels) / pixels_per_meter;
    const double magnitude = std::pow(10.0, std::floor(std::log10(wanted)));
    const double fraction = wanted / magnitude;
    return (fraction >= 5 - 1e-10 ? 5 : fraction >= 2 - 1e-10 ? 2 : 1) * magnitude;
  }
  static QString distance(double meters) {
    if (meters >= 1 - 1e-10) return QString::number(meters, 'g', 6) + " m";
    if (meters >= .01 - 1e-10) return QString::number(meters * 100, 'g', 6) + " cm";
    return QString::number(meters * 1000, 'g', 6) + " mm";
  }
};
}
