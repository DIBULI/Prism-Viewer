#include "ui/app_theme.hpp"

#include <QtCore/QDebug>
#include <QtGui/QColor>
#include <QtGui/QImage>
#include <QtGui/QPalette>
#include <QtWidgets/QAbstractItemView>
#include <QtWidgets/QApplication>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QTableWidget>
#include <QtWidgets/QVBoxLayout>

#include <cstdlib>

namespace {

bool expectColor(const char* description, const QColor& actual,
                 const char* expected_hex) {
  const QColor expected(QString::fromLatin1(expected_hex));
  if (actual == expected) return true;
  qCritical().noquote()
      << description << "expected" << expected.name(QColor::HexArgb)
      << "but got" << actual.name(QColor::HexArgb);
  return false;
}

int luminance(const QColor& color) {
  return (299 * color.red() + 587 * color.green() + 114 * color.blue()) /
         1000;
}

bool expectLightColor(const char* description, const QColor& actual) {
  if (luminance(actual) >= 160) return true;
  qCritical().noquote()
      << description << "must be light but got"
      << actual.name(QColor::HexArgb);
  return false;
}

double lightPixelRatio(QWidget& widget, int inset = 3) {
  const QSize size = widget.size();
  if (size.width() <= inset * 2 || size.height() <= inset * 2) return 0.0;

  QImage image(size, QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  widget.render(&image);

  int light_pixels = 0;
  int visible_pixels = 0;
  for (int y = inset; y < image.height() - inset; ++y) {
    for (int x = inset; x < image.width() - inset; ++x) {
      const QColor pixel = image.pixelColor(x, y);
      if (pixel.alpha() == 0) continue;
      ++visible_pixels;
      if (luminance(pixel) >= 160) ++light_pixels;
    }
  }
  return visible_pixels == 0
             ? 0.0
             : static_cast<double>(light_pixels) / visible_pixels;
}

bool expectLightRendering(const char* description, QWidget& widget,
                          double minimum_ratio) {
  const double ratio = lightPixelRatio(widget);
  if (ratio >= minimum_ratio) return true;
  qCritical() << description << "has only" << ratio * 100.0
              << "% light pixels; expected at least"
              << minimum_ratio * 100.0 << "%";
  return false;
}

}  // namespace

int main(int argc, char** argv) {
  QApplication application(argc, argv);

  // Reproduce the macOS failure deterministically on every platform by
  // starting from a deliberately dark application palette.
  QPalette dark_palette;
  dark_palette.setColor(QPalette::Window, QColor(QStringLiteral("#151515")));
  dark_palette.setColor(QPalette::WindowText,
                        QColor(QStringLiteral("#f0f0f0")));
  dark_palette.setColor(QPalette::Base, QColor(QStringLiteral("#202020")));
  dark_palette.setColor(QPalette::Text, QColor(QStringLiteral("#f0f0f0")));
  dark_palette.setColor(QPalette::Button, QColor(QStringLiteral("#202020")));
  dark_palette.setColor(QPalette::ButtonText,
                        QColor(QStringLiteral("#f0f0f0")));
  application.setPalette(dark_palette);

  prism_viewer::ui::applyLightApplicationTheme(application);

  bool ok = true;
  const QPalette application_palette = application.palette();
  ok &= expectColor("application Window",
                    application_palette.color(QPalette::Window), "#f5f7fb");
  ok &= expectColor("application WindowText",
                    application_palette.color(QPalette::WindowText),
                    "#182230");
  ok &= expectColor("application Base",
                    application_palette.color(QPalette::Base), "#ffffff");
  ok &= expectColor("application Text",
                    application_palette.color(QPalette::Text), "#182230");
  ok &= expectColor("disabled Base",
                    application_palette.color(QPalette::Disabled,
                                              QPalette::Base),
                    "#e4e7ec");

  QDialog dialog;
  dialog.setWindowTitle(QStringLiteral("Theme regression dialog"));
  dialog.resize(420, 230);
  auto* layout = new QVBoxLayout(&dialog);

  auto* table = new QTableWidget(1, 2, &dialog);
  table->setHorizontalHeaderLabels(
      {QStringLiteral("Section"), QStringLiteral("Field")});
  table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  table->setFixedHeight(100);
  layout->addWidget(table);

  auto* line_edit = new QLineEdit(&dialog);
  line_edit->setObjectName(QStringLiteral("themeTestLineEdit"));
  line_edit->setPlaceholderText(QStringLiteral("192.168.1.3"));
  layout->addWidget(line_edit);

  auto* combo_box = new QComboBox(&dialog);
  combo_box->setObjectName(QStringLiteral("themeTestComboBox"));
  combo_box->addItems({QString(), QStringLiteral("Mid-360")});
  layout->addWidget(combo_box);

  dialog.show();
  application.processEvents();

  ok &= expectColor("standalone dialog Window",
                    dialog.palette().color(QPalette::Window), "#f5f7fb");
  ok &= expectColor("line edit Base",
                    line_edit->palette().color(QPalette::Base), "#ffffff");
  ok &= expectColor("line edit Text",
                    line_edit->palette().color(QPalette::Text), "#182230");
  ok &= expectColor("combo box Button",
                    combo_box->palette().color(QPalette::Button), "#ffffff");
  ok &= expectColor("combo popup Base",
                    combo_box->view()->palette().color(QPalette::Base),
                    "#ffffff");
  // QStyleSheetStyle maps this role to the QHeaderView background on some Qt
  // versions and leaves it as the application Button role on others. Both are
  // valid, provided the inherited native dark color cannot leak through.
  ok &= expectLightColor(
      "table header Button",
      table->horizontalHeader()->palette().color(QPalette::Button));

  ok &= expectLightRendering("standalone dialog", dialog, 0.72);
  ok &= expectLightRendering("table header", *table->horizontalHeader(), 0.72);
  ok &= expectLightRendering("line edit", *line_edit, 0.75);
  ok &= expectLightRendering("combo box", *combo_box, 0.60);

  line_edit->setDisabled(true);
  combo_box->setDisabled(true);
  application.processEvents();
  ok &= expectColor("disabled line edit Base",
                    line_edit->palette().color(QPalette::Disabled,
                                               QPalette::Base),
                    "#e4e7ec");
  ok &= expectLightRendering("disabled line edit", *line_edit, 0.75);
  ok &= expectLightRendering("disabled combo box", *combo_box, 0.60);

  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
