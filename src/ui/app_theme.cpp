#include "ui/app_theme.hpp"

#include <QtCore/QString>
#include <QtCore/QtGlobal>
#include <QtGui/QColor>
#include <QtGui/QPalette>
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
#include <QtGui/QStyleHints>
#endif
#include <QtWidgets/QApplication>

namespace prism_viewer::ui {

void applyLightApplicationTheme(QApplication& application) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
  // On current Qt versions this also asks the platform style to draw native
  // subcontrols (for example scroll bars) using its light appearance.
  application.styleHints()->setColorScheme(Qt::ColorScheme::Light);
#endif

  QPalette palette;
  palette.setColor(QPalette::Window, QColor(QStringLiteral("#f5f7fb")));
  palette.setColor(QPalette::WindowText, QColor(QStringLiteral("#182230")));
  palette.setColor(QPalette::Base, QColor(QStringLiteral("#ffffff")));
  palette.setColor(QPalette::AlternateBase,
                   QColor(QStringLiteral("#f7f9fc")));
  palette.setColor(QPalette::ToolTipBase,
                   QColor(QStringLiteral("#ffffff")));
  palette.setColor(QPalette::ToolTipText,
                   QColor(QStringLiteral("#182230")));
  palette.setColor(QPalette::Text, QColor(QStringLiteral("#182230")));
  palette.setColor(QPalette::Button, QColor(QStringLiteral("#ffffff")));
  palette.setColor(QPalette::ButtonText, QColor(QStringLiteral("#182230")));
  palette.setColor(QPalette::BrightText, QColor(QStringLiteral("#ffffff")));
  palette.setColor(QPalette::Light, QColor(QStringLiteral("#ffffff")));
  palette.setColor(QPalette::Midlight, QColor(QStringLiteral("#f2f4f7")));
  palette.setColor(QPalette::Mid, QColor(QStringLiteral("#d0d5dd")));
  palette.setColor(QPalette::Dark, QColor(QStringLiteral("#98a2b3")));
  palette.setColor(QPalette::Shadow, QColor(QStringLiteral("#667085")));
  palette.setColor(QPalette::Highlight, QColor(QStringLiteral("#1557d2")));
  palette.setColor(QPalette::HighlightedText,
                   QColor(QStringLiteral("#ffffff")));
  palette.setColor(QPalette::Link, QColor(QStringLiteral("#1557d2")));
  palette.setColor(QPalette::LinkVisited,
                   QColor(QStringLiteral("#6941c6")));
#if QT_VERSION >= QT_VERSION_CHECK(5, 12, 0)
  palette.setColor(QPalette::PlaceholderText,
                   QColor(QStringLiteral("#98a2b3")));
#endif
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
  palette.setColor(QPalette::Accent, QColor(QStringLiteral("#1557d2")));
#endif

  palette.setColor(QPalette::Disabled, QPalette::WindowText,
                   QColor(QStringLiteral("#98a2b3")));
  palette.setColor(QPalette::Disabled, QPalette::Text,
                   QColor(QStringLiteral("#98a2b3")));
  palette.setColor(QPalette::Disabled, QPalette::ButtonText,
                   QColor(QStringLiteral("#98a2b3")));
  palette.setColor(QPalette::Disabled, QPalette::Base,
                   QColor(QStringLiteral("#e4e7ec")));
  palette.setColor(QPalette::Disabled, QPalette::Button,
                   QColor(QStringLiteral("#e4e7ec")));
  palette.setColor(QPalette::Disabled, QPalette::Highlight,
                   QColor(QStringLiteral("#b8c7da")));
  palette.setColor(QPalette::Disabled, QPalette::HighlightedText,
                   QColor(QStringLiteral("#667085")));
#if QT_VERSION >= QT_VERSION_CHECK(5, 12, 0)
  palette.setColor(QPalette::Disabled, QPalette::PlaceholderText,
                   QColor(QStringLiteral("#98a2b3")));
#endif
  application.setPalette(palette);

  // Application scope is intentional: standalone dialogs are not children of
  // MainWindow's central widget and otherwise fall back to the macOS dark
  // palette. Keep this sheet focused on controls whose native dark rendering
  // remains visible through the Viewer's existing per-panel style sheets.
  application.setStyleSheet(QStringLiteral(R"QSS(
QToolTip {
  background-color: #ffffff;
  color: #182230;
  border: 1px solid #c9d4e2;
}
QHeaderView {
  background-color: #f5f7fb;
}
QHeaderView::section,
QTableCornerButton::section {
  background-color: #e9eef5;
  color: #24364d;
  border: 0;
  border-right: 1px solid #c9d4e2;
  border-bottom: 1px solid #c9d4e2;
  padding: 6px 8px;
  font-weight: 600;
}
QLineEdit,
QAbstractSpinBox,
QComboBox {
  background-color: #ffffff;
  color: #182230;
  border: 1px solid #c9d4e2;
  border-radius: 7px;
  padding: 6px 9px;
  selection-background-color: #1557d2;
  selection-color: #ffffff;
}
QLineEdit:focus,
QAbstractSpinBox:focus,
QComboBox:focus {
  border-color: #4b83d1;
}
QLineEdit:disabled,
QAbstractSpinBox:disabled,
QComboBox:disabled {
  background-color: #e4e7ec;
  color: #98a2b3;
  border-color: #d0d5dd;
}
QComboBox::drop-down {
  subcontrol-origin: padding;
  subcontrol-position: top right;
  width: 24px;
  border-left: 1px solid #d0d5dd;
}
QComboBox QAbstractItemView {
  background-color: #ffffff;
  alternate-background-color: #f7f9fc;
  color: #182230;
  border: 1px solid #c9d4e2;
  outline: 0;
  selection-background-color: #1557d2;
  selection-color: #ffffff;
}
QComboBox QAbstractItemView::item {
  min-height: 24px;
  padding: 3px 8px;
}
)QSS"));
}

}  // namespace prism_viewer::ui
