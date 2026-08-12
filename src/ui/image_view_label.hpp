#pragma once

#include <QtGui/QImage>
#include <QtGui/QPixmap>
#include <QtWidgets/QLabel>

#include <functional>

class QMouseEvent;
class QResizeEvent;

namespace prism_viewer::ui {

// Aspect-fit image label whose source pixmap never dictates layout geometry.
// This is important for live camera tiles: QLabel normally feeds the pixmap
// size back into its size hint when the first frame arrives, which can make a
// top-level window grow after capture starts.
class ImageViewLabel final : public QLabel {
 public:
  explicit ImageViewLabel(QWidget* parent = nullptr);

  void setTransformationMode(Qt::TransformationMode mode);
  void setImage(const QImage& image);
  void clearImage(const QString& text);

  std::function<void()> on_click;
  std::function<void(const QSize&)> on_resize;

 protected:
  void resizeEvent(QResizeEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;

 private:
  void refreshPixmap();

  QPixmap source_pixmap_;
  Qt::TransformationMode transformation_mode_ = Qt::SmoothTransformation;
};

}  // namespace prism_viewer::ui
