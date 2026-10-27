#include "common/ui_text.hpp"
#include "ui/camera_zoom_dialog.hpp"
#include "ui/zoomable_image_view.hpp"

#include <QtGui/QMouseEvent>
#include <QtWidgets/QApplication>
#include <QtWidgets/QGraphicsPixmapItem>
#include <QtWidgets/QGraphicsScene>
#include <QtWidgets/QGraphicsSimpleTextItem>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>

#include <array>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

QSize displayedSize(const prism_viewer::ui::ZoomableImageView& view) {
  for (auto* item : view.scene()->items())
    if (auto* pixmap = dynamic_cast<QGraphicsPixmapItem*>(item))
      if (pixmap->isVisible()) return pixmap->pixmap().size();
  return {};
}

QString emptyMessage(const prism_viewer::ui::ZoomableImageView& view) {
  for (auto* item : view.scene()->items())
    if (auto* text = dynamic_cast<QGraphicsSimpleTextItem*>(item))
      if (text->isVisible()) return text->text();
  return {};
}

qint64 displayedKey(const prism_viewer::ui::ZoomableImageView& view) {
  for (auto* item : view.scene()->items())
    if (auto* pixmap = dynamic_cast<QGraphicsPixmapItem*>(item))
      if (pixmap->isVisible()) return pixmap->pixmap().cacheKey();
  return 0;
}

void run(bool chinese) {
  using namespace prism_viewer;
  common::setChineseUi(chinese);
  ui::CameraZoomDialog dialog;
  auto* view = dynamic_cast<ui::ZoomableImageView*>(
      dialog.findChild<QGraphicsView*>(QStringLiteral("cameraZoomImage")));
  auto* actual = dialog.findChild<QPushButton*>(QStringLiteral("cameraZoomActualSize"));
  require(view && actual, "missing zoom controls");
  std::array<QImage, 4> images;
  std::array<bool, 4> full{};
  for (auto& image : images) {
    image = QImage(640, 384, QImage::Format_Grayscale8);
    image.fill(80);
  }
  dialog.setImageSet(images, 0, QStringLiteral("Live"), full);
  dialog.show();
  QApplication::processEvents();
  require(!view->hasImage() && !actual->isEnabled(), "thumbnail enlarged on open");
  require(emptyMessage(*view) == common::uiText(
      "Waiting for a full-resolution frame…", "正在等待完整分辨率图像……"),
      "missing localized full-resolution waiting message");

  images[0] = QImage(5120, 3072, QImage::Format_Grayscale8);
  images[0].fill(101);
  full[0] = true;
  dialog.setImageSet(images, 0, QStringLiteral("RAW12"), full);
  require(displayedSize(*view) == QSize(5120, 3072), "RAW source pixels reduced");
  actual->click();
  require(view->zoomPercent() == 100, "100 percent is not native size");
  const auto unchanged_key = displayedKey(*view);
  dialog.setImageSet(images, 0, QStringLiteral("RAW12 next frame"), full);
  require(view->zoomPercent() == 100, "live update changed zoom");
  require(displayedKey(*view) == unchanged_key,
          "unchanged selected image was converted/uploaded again");
  images[3].fill(90);
  dialog.setImageSet(images, 0, QStringLiteral("Other lane updated"), full);
  require(displayedKey(*view) == unchanged_key,
          "other lane update reuploaded the selected image");
  images[0].fill(102);
  dialog.setImageSet(images, 0, QStringLiteral("New selected image"), full);
  require(displayedKey(*view) != unchanged_key,
          "new image content was incorrectly deduplicated");

  int selected = -1;
  dialog.on_selected_camera_changed = [&](int camera) { selected = camera; };
  auto* thumb = dialog.findChild<QLabel*>(QStringLiteral("cameraZoomThumbnail1"));
  require(thumb != nullptr, "missing camera selector");
  QMouseEvent click(QEvent::MouseButtonPress, QPointF(10, 10), Qt::LeftButton,
                    Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(thumb, &click);
  require(selected == 1 && dialog.selectedCamera() == 1 && !view->hasImage(),
          "camera switch enlarged a thumbnail or retained another camera");

  images[1] = QImage(1280, 1024, QImage::Format_RGB32);
  images[1].fill(Qt::red);
  full[1] = true;
  dialog.setImageSet(images, 1, QStringLiteral("JPEG"), full);
  require(displayedSize(*view) == QSize(1280, 1024), "JPEG source pixels reduced");
  const auto jpeg_key = displayedKey(*view);
  for (int n = 0; n < 8; ++n) {
    dialog.hide();
    dialog.setImageSet(images, 1, QStringLiteral("Reopen JPEG"), full);
    dialog.show();
    QApplication::processEvents();
    require(displayedKey(*view) == jpeg_key, "reopen reuploaded unchanged JPEG");
  }
  // Provenance, not dimensions or pixel identity, grants full-resolution use.
  full[1] = false;
  dialog.setImageSet(images, 1, QStringLiteral("Same pixels, thumbnail provenance"), full);
  require(!view->hasImage(), "same-image thumbnail provenance was ignored");
  full[1] = true;
  dialog.setImageSet(images, 1, QStringLiteral("Full resolution restored"), full);
  require(displayedSize(*view) == QSize(1280, 1024), "same-image full frame not restored");
  // A preview already in flight when selection changed cannot overwrite the
  // main image with an upscaled thumbnail, even if it is a newer frame.
  images[1] = images[1].scaled(640, 512);
  full[1] = false;
  dialog.setImageSet(images, 1, QStringLiteral("Late thumbnail"), full);
  require(!view->hasImage() && !actual->isEnabled(), "late thumbnail enlarged");
  dialog.hide();
  dialog.show();
  require(!view->hasImage(), "reopen enlarged cached thumbnail");

  images[1] = QImage();
  dialog.setImageSet(images, 1, QStringLiteral("Decode failed"), full);
  require(!view->hasImage() && emptyMessage(*view) == common::uiText("No frame", "无图像"),
          "missing frame retained stale content");
  // Dataset replay explicitly supplies full-sized images. Size alone is not
  // a reliable thumbnail discriminator: native small images are valid too.
  dialog.setImageSet(images, 2, QStringLiteral("Dataset"), {true, true, true, true});
  require(displayedSize(*view) == images[2].size(), "native small dataset image rejected");
  dialog.hide();
}
}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  try {
    run(false);
    run(true);
    std::cout << "camera_zoom_dialog_test=PASS languages=en,zh RAW=5120x3072 JPEG=1280x1024\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
