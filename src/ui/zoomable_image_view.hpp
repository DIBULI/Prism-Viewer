#pragma once

#include <QtGui/QImage>
#include <QtWidgets/QGraphicsView>

#include <functional>

class QGraphicsPixmapItem;
class QGraphicsSimpleTextItem;
class QMouseEvent;
class QResizeEvent;
class QWheelEvent;

namespace prism_viewer::ui {

// Full-resolution image view with a persistent scale/pan transform. Live frame
// updates replace only the scene pixmap, so the user's zoom does not jump.
class ZoomableImageView final : public QGraphicsView {
 public:
  explicit ZoomableImageView(QWidget* parent = nullptr);

  void setImage(const QImage& image);
  void clearImage(const QString& message);
  bool hasImage() const;

  void fitToWindow();
  void actualSize();
  void zoomIn();
  void zoomOut();
  int zoomPercent() const;

  std::function<void(int)> on_zoom_changed;

 protected:
  void resizeEvent(QResizeEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;

 private:
  void applyZoom(double requested_scale);
  void updateZoomNotification();

  QGraphicsPixmapItem* pixmap_item_ = nullptr;
  QGraphicsSimpleTextItem* empty_text_item_ = nullptr;
  bool fit_to_window_ = true;
  bool fitting_ = false;
  int last_reported_zoom_percent_ = -1;
};

}  // namespace prism_viewer::ui
