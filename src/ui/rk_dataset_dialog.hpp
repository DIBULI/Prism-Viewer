#pragma once
#include <QtCore/QString>
#include <functional>
class QWidget;
namespace prism_viewer::ui {
void showRkDatasetDialog(QWidget* parent, const std::function<void(const QString&)>& open_dataset);
}
