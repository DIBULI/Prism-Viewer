#include "ui/camera_zoom_dialog.hpp"

#include "common/ui_text.hpp"
#include "ui/zoomable_image_view.hpp"

#include <QtGui/QHideEvent>
#include <QtGui/QMouseEvent>
#include <QtGui/QPixmap>
#include <QtGui/QResizeEvent>
#include <QtGui/QShowEvent>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QVBoxLayout>

namespace prism_viewer::ui {
namespace {

constexpr auto kThumbnailRefreshPeriod = std::chrono::milliseconds(250);

}  // namespace

class CameraThumbnailLabel final : public QLabel {
 public:
  explicit CameraThumbnailLabel(QWidget* parent = nullptr) : QLabel(parent) {
    setAlignment(Qt::AlignCenter);
  }

  void setImage(const QImage& image) {
    image_ = image;
    refreshPixmap();
  }

  void clearImage(const QString& message) {
    image_ = QImage();
    setPixmap(QPixmap());
    setText(message);
  }

  std::function<void()> on_click;

 protected:
  void resizeEvent(QResizeEvent* event) override {
    QLabel::resizeEvent(event);
    refreshPixmap();
  }

  void mousePressEvent(QMouseEvent* event) override {
    if (event->button() == Qt::LeftButton && !image_.isNull() && on_click) {
      on_click();
      event->accept();
      return;
    }
    QLabel::mousePressEvent(event);
  }

 private:
  void refreshPixmap() {
    if (image_.isNull() || width() <= 0 || height() <= 0) return;
    setPixmap(QPixmap::fromImage(image_).scaled(
        size(), Qt::KeepAspectRatio, Qt::FastTransformation));
  }

  QImage image_;
};

CameraZoomDialog::CameraZoomDialog(QWidget* parent) : QDialog(parent) {
  setWindowTitle(common::uiText("Camera image", "相机图像"));
  setMinimumSize(880, 620);
  resize(1240, 880);
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(12, 12, 12, 12);
  root->setSpacing(10);

  auto* header = new QHBoxLayout();
  context_label_ = new QLabel(this);
  context_label_->setStyleSheet(QStringLiteral(
      "font-size: 12pt; font-weight: 600; color: #344054;"));
  header->addWidget(context_label_, 1);

  zoom_out_button_ = new QPushButton(QStringLiteral("−"), this);
  zoom_out_button_->setToolTip(common::uiText("Zoom out", "缩小"));
  actual_size_button_ = new QPushButton(QStringLiteral("100%"), this);
  actual_size_button_->setToolTip(common::uiText(
      "Show one image pixel per screen pixel", "按原始像素显示"));
  fit_button_ =
      new QPushButton(common::uiText("Fit", "适应窗口"), this);
  fit_button_->setToolTip(common::uiText(
      "Fit the whole image in the window", "完整图像适应窗口"));
  zoom_in_button_ = new QPushButton(QStringLiteral("+"), this);
  zoom_in_button_->setToolTip(common::uiText("Zoom in", "放大"));
  zoom_label_ = new QLabel(QStringLiteral("100%"), this);
  zoom_label_->setAlignment(Qt::AlignCenter);
  zoom_label_->setMinimumWidth(58);

  zoom_out_button_->setMinimumWidth(44);
  actual_size_button_->setMinimumWidth(66);
  fit_button_->setMinimumWidth(88);
  zoom_in_button_->setMinimumWidth(44);
  header->addWidget(zoom_out_button_);
  header->addWidget(actual_size_button_);
  header->addWidget(fit_button_);
  header->addWidget(zoom_in_button_);
  header->addWidget(zoom_label_);
  root->addLayout(header);

  main_image_ = new ZoomableImageView(this);
  main_image_->setMinimumSize(760, 460);
  main_image_->setStyleSheet(QStringLiteral(
      "background: #111827; border: 1px solid #344054; border-radius: 8px;"));
  main_image_->on_zoom_changed = [this](int percent) {
    zoom_label_->setText(QStringLiteral("%1%").arg(percent));
  };
  root->addWidget(main_image_, 1);

  auto* hint = new QLabel(
      common::uiText(
          "Mouse wheel: zoom  |  Drag: pan  |  Double-click: fit",
          "鼠标滚轮：缩放  |  拖动：平移  |  双击：适应窗口"),
      this);
  hint->setAlignment(Qt::AlignCenter);
  hint->setStyleSheet(QStringLiteral("color: #667085;"));
  root->addWidget(hint);

  auto* thumbnails = new QHBoxLayout();
  thumbnails->setSpacing(10);
  for (int camera = 0; camera < 4; ++camera) {
    auto* stack = new QVBoxLayout();
    auto* caption = new QLabel(
        common::uiText("Camera %1", "相机 %1").arg(camera), this);
    caption->setAlignment(Qt::AlignCenter);
    thumbnail_labels_[camera] = new CameraThumbnailLabel(this);
    thumbnail_labels_[camera]->setMinimumSize(160, 100);
    thumbnail_labels_[camera]->setMaximumHeight(140);
    thumbnail_labels_[camera]->setCursor(Qt::PointingHandCursor);
    thumbnail_labels_[camera]->on_click =
        [this, camera]() { selectCamera(camera); };
    stack->addWidget(caption);
    stack->addWidget(thumbnail_labels_[camera], 1);
    thumbnails->addLayout(stack, 1);
  }
  root->addLayout(thumbnails);

  connect(zoom_out_button_, &QPushButton::clicked, main_image_,
          &ZoomableImageView::zoomOut);
  connect(actual_size_button_, &QPushButton::clicked, main_image_,
          &ZoomableImageView::actualSize);
  connect(fit_button_, &QPushButton::clicked, main_image_,
          &ZoomableImageView::fitToWindow);
  connect(zoom_in_button_, &QPushButton::clicked, main_image_,
          &ZoomableImageView::zoomIn);

  main_image_->clearImage(common::uiText("No frame", "无图像"));
  updateZoomControls();
}

void CameraZoomDialog::setImageSet(const std::array<QImage, 4>& images,
                                   int selected_camera,
                                   const QString& context) {
  images_ = images;
  bool selection_changed = false;
  if (selected_camera >= 0 && selected_camera < 4) {
    selection_changed = selected_camera_ != selected_camera;
    selected_camera_ = selected_camera;
  }
  context_ = context;
  const auto now = std::chrono::steady_clock::now();
  const bool refresh_thumbnails =
      selection_changed || now >= next_thumbnail_refresh_;
  if (refresh_thumbnails) {
    next_thumbnail_refresh_ = now + kThumbnailRefreshPeriod;
  }
  refresh(refresh_thumbnails, selection_changed);
}

int CameraZoomDialog::selectedCamera() const { return selected_camera_; }

void CameraZoomDialog::showEvent(QShowEvent* event) {
  QDialog::showEvent(event);
  main_image_->fitToWindow();
  if (on_visibility_changed) on_visibility_changed(true);
}

void CameraZoomDialog::hideEvent(QHideEvent* event) {
  QDialog::hideEvent(event);
  if (on_visibility_changed) on_visibility_changed(false);
}

void CameraZoomDialog::selectCamera(int camera) {
  if (camera < 0 || camera >= 4 || camera == selected_camera_) return;
  selected_camera_ = camera;
  if (on_selected_camera_changed) {
    on_selected_camera_changed(selected_camera_);
  }
  refresh(true, true);
}

void CameraZoomDialog::refresh(bool refresh_thumbnails, bool reset_view) {
  context_label_->setText(
      common::uiText("%1 | enlarged Camera %2", "%1 | 放大相机 %2")
          .arg(context_)
          .arg(selected_camera_));
  if (images_[selected_camera_].isNull()) {
    main_image_->clearImage(common::uiText("No frame", "无图像"));
  } else {
    main_image_->setImage(images_[selected_camera_]);
    if (reset_view) main_image_->fitToWindow();
  }

  if (refresh_thumbnails) {
    for (int camera = 0; camera < 4; ++camera) {
      if (images_[camera].isNull()) {
        thumbnail_labels_[camera]->clearImage(
            common::uiText("No frame", "无图像"));
      } else {
        thumbnail_labels_[camera]->setImage(images_[camera]);
      }
      thumbnail_labels_[camera]->setStyleSheet(
          camera == selected_camera_
              ? QStringLiteral(
                    "background: #111827; border: 3px solid #1557d2;"
                    "border-radius: 6px; color: #d0d5dd;")
              : QStringLiteral(
                    "background: #111827; border: 1px solid #667085;"
                    "border-radius: 6px; color: #d0d5dd;"));
    }
  }
  updateZoomControls();
}

void CameraZoomDialog::updateZoomControls() {
  const bool enabled = main_image_->hasImage();
  zoom_out_button_->setEnabled(enabled);
  actual_size_button_->setEnabled(enabled);
  fit_button_->setEnabled(enabled);
  zoom_in_button_->setEnabled(enabled);
}

}  // namespace prism_viewer::ui
