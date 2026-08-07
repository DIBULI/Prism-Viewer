#pragma once

#include <QtGui/QImage>
#include <QtWidgets/QDialog>

#include <array>
#include <chrono>
#include <functional>

class QLabel;
class QPushButton;
class QHideEvent;
class QShowEvent;

namespace prism_viewer::ui {

class ZoomableImageView;
class CameraThumbnailLabel;

class CameraZoomDialog final : public QDialog {
 public:
  explicit CameraZoomDialog(QWidget* parent = nullptr);

  void setImageSet(const std::array<QImage, 4>& images, int selected_camera,
                   const QString& context);
  int selectedCamera() const;

  std::function<void(int)> on_selected_camera_changed;
  std::function<void(bool)> on_visibility_changed;

 protected:
  void showEvent(QShowEvent* event) override;
  void hideEvent(QHideEvent* event) override;

 private:
  void selectCamera(int camera);
  void refresh(bool refresh_thumbnails, bool reset_view);
  void updateZoomControls();

  QLabel* context_label_ = nullptr;
  QLabel* zoom_label_ = nullptr;
  QPushButton* zoom_out_button_ = nullptr;
  QPushButton* actual_size_button_ = nullptr;
  QPushButton* fit_button_ = nullptr;
  QPushButton* zoom_in_button_ = nullptr;
  ZoomableImageView* main_image_ = nullptr;
  std::array<CameraThumbnailLabel*, 4> thumbnail_labels_{};
  std::array<QImage, 4> images_{};
  std::chrono::steady_clock::time_point next_thumbnail_refresh_{};
  int selected_camera_ = 0;
  QString context_;
};

}  // namespace prism_viewer::ui
