#include "ui/image_view_label.hpp"

#include <QtWidgets/QApplication>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QSizePolicy>
#include <QtWidgets/QWidget>

#include <array>
#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);

  QWidget window;
  auto* layout = new QGridLayout(&window);
  std::array<prism_viewer::ui::ImageViewLabel*, 4> views{};
  for (int camera = 0; camera < 4; ++camera) {
    views[camera] = new prism_viewer::ui::ImageViewLabel(&window);
    views[camera]->setMinimumSize(280, 200);
    views[camera]->clearImage(QStringLiteral("No frame"));
    layout->addWidget(views[camera], camera / 2, camera % 2);
    require(views[camera]->sizePolicy().horizontalPolicy() ==
                    QSizePolicy::Ignored &&
                views[camera]->sizePolicy().verticalPolicy() ==
                    QSizePolicy::Ignored,
            "image dimensions are excluded from layout size hints");
  }

  window.resize(900, 620);
  window.show();
  QApplication::processEvents();
  const QSize window_before_frames = window.size();
  const QSize minimum_before_frames = window.minimumSizeHint();

  QImage frame(1280, 1024, QImage::Format_RGB888);
  frame.fill(Qt::green);
  for (auto* view : views) view->setImage(frame);
  QApplication::processEvents();

  require(window.size() == window_before_frames,
          "first camera frames do not resize the containing window");
  require(window.minimumSizeHint() == minimum_before_frames,
          "first camera frames do not increase the layout minimum size");
  for (auto* view : views) {
    require(view->minimumSize() == QSize(280, 200),
            "explicit camera tile minimum remains enforced");
  }

  std::cout << "image view label tests passed\n";
  return 0;
}
