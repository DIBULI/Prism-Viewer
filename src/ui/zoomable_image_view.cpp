#include "ui/zoomable_image_view.hpp"
#include "ui/bayer_preview.hpp"

#include <QtGui/QColor>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainter>
#include <QtGui/QResizeEvent>
#include <QtGui/QWheelEvent>
#include <QtWidgets/QGraphicsPixmapItem>
#include <QtWidgets/QGraphicsScene>
#include <QtWidgets/QGraphicsSimpleTextItem>

#include <algorithm>
#include <cmath>

namespace prism_viewer::ui {
namespace {

constexpr double kMinimumScale = 0.10;
constexpr double kMaximumScale = 8.0;
constexpr double kWheelScaleStep = 1.20;

}  // namespace

ZoomableImageView::ZoomableImageView(QWidget* parent) : QGraphicsView(parent) {
  auto* image_scene = new QGraphicsScene(this);
  setScene(image_scene);
  pixmap_item_ = image_scene->addPixmap(QPixmap());
  pixmap_item_->setTransformationMode(Qt::SmoothTransformation);
  empty_text_item_ = image_scene->addSimpleText(QString());
  empty_text_item_->setBrush(QColor(QStringLiteral("#d0d5dd")));

  setAlignment(Qt::AlignCenter);
  setBackgroundBrush(QColor(QStringLiteral("#111827")));
  setFrameShape(QFrame::NoFrame);
  setRenderHint(QPainter::SmoothPixmapTransform, true);
  setDragMode(QGraphicsView::ScrollHandDrag);
  setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
  setResizeAnchor(QGraphicsView::AnchorViewCenter);
  setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
}

void ZoomableImageView::setImage(const QImage& image) {
  if (image.isNull()) {
    clearImage(QString());
    return;
  }

  const bool had_image = hasImage();
  const QSize previous_size = native_bayer_image_.isNull()
      ? pixmap_item_->pixmap().size() : native_bayer_image_.size();
  native_bayer_image_ = isNativeBayerPreview(image) ? image : QImage();
  averaged_bayer_image_ = native_bayer_image_.isNull()
      ? QImage() : raw12ThumbnailSource(image);
  showing_bayer_average_ = false;
  pixmap_item_->setTransform(QTransform());
  pixmap_item_->setPixmap(QPixmap::fromImage(raw12SrgbDisplay(image)));
  pixmap_item_->setVisible(true);
  empty_text_item_->setVisible(false);
  scene()->setSceneRect(pixmap_item_->boundingRect());

  if (!had_image || previous_size != image.size()) {
    fitToWindow();
  } else {
    updateZoomNotification();
  }
}

void ZoomableImageView::clearImage(const QString& message) {
  native_bayer_image_ = {};
  averaged_bayer_image_ = {};
  showing_bayer_average_ = false;
  pixmap_item_->setTransform(QTransform());
  pixmap_item_->setPixmap(QPixmap());
  pixmap_item_->setVisible(false);
  empty_text_item_->setText(message);
  empty_text_item_->setVisible(!message.isEmpty());
  const QRectF text_bounds = empty_text_item_->boundingRect();
  empty_text_item_->setPos(-text_bounds.width() / 2.0,
                           -text_bounds.height() / 2.0);
  scene()->setSceneRect(text_bounds.adjusted(-24.0, -16.0, 24.0, 16.0));
  resetTransform();
  fit_to_window_ = true;
  updateZoomNotification();
}

bool ZoomableImageView::hasImage() const {
  return pixmap_item_ != nullptr && !pixmap_item_->pixmap().isNull();
}

void ZoomableImageView::fitToWindow() {
  if (fitting_) return;
  fitting_ = true;
  fit_to_window_ = true;
  resetTransform();
  if (hasImage() && !pixmap_item_->boundingRect().isEmpty()) {
    if (native_bayer_image_.isNull()) fitInView(pixmap_item_, Qt::KeepAspectRatio);
    else fitInView(scene()->sceneRect(), Qt::KeepAspectRatio);
  }
  fitting_ = false;
  updateZoomNotification();
}

void ZoomableImageView::actualSize() {
  if (!hasImage()) return;
  fit_to_window_ = false;
  resetTransform();
  centerOn(pixmap_item_);
  updateZoomNotification();
}

void ZoomableImageView::zoomIn() {
  if (!hasImage()) return;
  applyZoom(transform().m11() * kWheelScaleStep);
}

void ZoomableImageView::zoomOut() {
  if (!hasImage()) return;
  applyZoom(transform().m11() / kWheelScaleStep);
}

int ZoomableImageView::zoomPercent() const {
  return static_cast<int>(std::lround(transform().m11() * 100.0));
}

void ZoomableImageView::resizeEvent(QResizeEvent* event) {
  QGraphicsView::resizeEvent(event);
  if (fit_to_window_ && !fitting_) fitToWindow();
}

void ZoomableImageView::wheelEvent(QWheelEvent* event) {
  if (!hasImage() || event->angleDelta().y() == 0) {
    QGraphicsView::wheelEvent(event);
    return;
  }

  const double factor =
      event->angleDelta().y() > 0 ? kWheelScaleStep : 1.0 / kWheelScaleStep;
  applyZoom(transform().m11() * factor);
  event->accept();
}

void ZoomableImageView::mouseDoubleClickEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton && hasImage()) {
    fitToWindow();
    event->accept();
    return;
  }
  QGraphicsView::mouseDoubleClickEvent(event);
}

void ZoomableImageView::applyZoom(double requested_scale) {
  if (!hasImage()) return;
  fit_to_window_ = false;
  const double target =
      std::clamp(requested_scale, kMinimumScale, kMaximumScale);
  const double current = transform().m11();
  if (current <= 0.0) return;
  scale(target / current, target / current);
  updateZoomNotification();
}

void ZoomableImageView::updateZoomNotification() {
  updateBayerPixmap();
  const int percent = zoomPercent();
  if (percent == last_reported_zoom_percent_) return;
  last_reported_zoom_percent_ = percent;
  if (on_zoom_changed) on_zoom_changed(percent);
}

void ZoomableImageView::updateBayerPixmap() {
  if (native_bayer_image_.isNull() || averaged_bayer_image_.isNull()) return;
  // Under 100%, remove the CFA carrier before the scene's interpolation.
  // Item coordinates remain native pixels, keeping zoom/pan/100% semantics.
  const bool average = transform().m11() < 1.0 - 1e-9;
  if (average == showing_bayer_average_) return;
  pixmap_item_->setPixmap(QPixmap::fromImage(
      raw12SrgbDisplay(average ? averaged_bayer_image_ : native_bayer_image_)));
  pixmap_item_->setTransform(average ? QTransform::fromScale(2.0, 2.0)
                                   : QTransform());
  showing_bayer_average_ = average;
}

}  // namespace prism_viewer::ui
