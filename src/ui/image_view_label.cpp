#include "ui/image_view_label.hpp"
#include "ui/bayer_preview.hpp"

#include <QtGui/QMouseEvent>
#include <QtGui/QResizeEvent>
#include <QtWidgets/QSizePolicy>

namespace prism_viewer::ui {

ImageViewLabel::ImageViewLabel(QWidget* parent) : QLabel(parent) {
  setAlignment(Qt::AlignCenter);

  // QLabel's default Preferred policy lets a newly assigned pixmap replace
  // the small text size hint. In a 2x2 camera grid that layout request can
  // enlarge the top-level window as soon as the first frame is displayed.
  // Explicit minimum sizes still apply; only the frame-derived hint is
  // ignored, so the existing window geometry remains authoritative.
  setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
}

void ImageViewLabel::setTransformationMode(Qt::TransformationMode mode) {
  transformation_mode_ = mode;
  refreshPixmap();
}

void ImageViewLabel::setImage(const QImage& image) {
  linear_raw12_image_ = isRaw12Preview(image) ? raw12ThumbnailSource(image) : QImage();
  source_pixmap_ = isRaw12Preview(image) ? QPixmap() : QPixmap::fromImage(image);
  refreshPixmap();
}

void ImageViewLabel::clearImage(const QString& text) {
  source_pixmap_ = QPixmap();
  linear_raw12_image_ = QImage();
  setPixmap(QPixmap());
  setText(text);
}

void ImageViewLabel::resizeEvent(QResizeEvent* event) {
  QLabel::resizeEvent(event);
  refreshPixmap();
  if (on_resize) on_resize(event->size());
}

void ImageViewLabel::mousePressEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton &&
      (!source_pixmap_.isNull() || !linear_raw12_image_.isNull()) &&
      on_click) {
    on_click();
    event->accept();
    return;
  }
  QLabel::mousePressEvent(event);
}

void ImageViewLabel::refreshPixmap() {
  if (!linear_raw12_image_.isNull() && width() > 0 && height() > 0) {
    // Filtering must operate on linear light, not sRGB-encoded CFA values.
    const auto scaled = linear_raw12_image_.scaled(
        size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    setPixmap(QPixmap::fromImage(raw12SrgbDisplay(scaled)));
    return;
  }
  if (source_pixmap_.isNull() || width() <= 0 || height() <= 0) return;
  const QSize target_size =
      source_pixmap_.size().scaled(size(), Qt::KeepAspectRatio);
  if (target_size == source_pixmap_.size()) {
    setPixmap(source_pixmap_);
    return;
  }
  setPixmap(source_pixmap_.scaled(target_size, Qt::IgnoreAspectRatio,
      transformation_mode_));
}

}  // namespace prism_viewer::ui
