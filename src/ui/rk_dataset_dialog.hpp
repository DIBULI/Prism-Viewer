#pragma once
#include <QtCore/QString>
#include <functional>
#include "dataset/rk_dataset_client.hpp"
class QWidget;
namespace prism_viewer::ui {
void showRkDatasetDialog(QWidget* parent, dataset::RkDatasetAccess access,
                         const std::function<void(const QString&)>& open_dataset);
}
